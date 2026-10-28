#include "camera_webserver/calibration/calibration_handler.hpp"

#include <algorithm>
#include <limits>
#include <nlohmann/json.hpp>
#include <rclcpp/logging.hpp>

using json = nlohmann::json;

namespace camera_web::calibration {

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

CalibrationHandler::CalibrationHandler(std::shared_ptr<basin::web_core::WebNode> web_node,
                                       CalibrationHandlerConfig config) :
    config_(config) {
    // Create a mutually-exclusive callback group so the dedicated executor
    // can spin the service client without conflicting with the main executor.
    client_cbg_ =
        web_node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive, false /* don't auto-add */);

    // Service client bound to the isolated callback group.
    cal_client_ = web_node->create_client<CamCalRequest>(config_.service_name, rclcpp::ServicesQoS(), client_cbg_);

    // Dedicated executor – only spins client_cbg_.
    client_executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
    client_executor_->add_callback_group(client_cbg_, web_node->get_node_base_interface());

    // Subscribe to the status topic (main executor processes this callback).
    status_sub_ = web_node->create_subscription<CamCalStatus>(
        config_.status_topic, rclcpp::SystemDefaultsQoS(),
        [this](const CamCalStatus::SharedPtr msg) {
            std::lock_guard<std::mutex> lock(status_mutex_);
            latest_status_ = *msg;
        });

    // -----------------------------------------------------------------------
    // REST routes
    // -----------------------------------------------------------------------

    // GET /api/calibration/cameras
    web_node->getServer().Get("/api/calibration/cameras", [this](const httplib::Request&, httplib::Response& res) {
        json j = json::array();
        for (const auto& cam : getCamerasForCalibration()) {
            j.push_back(cam);
        }
        res.set_content(j.dump(), "application/json");
    });

    // GET /api/calibration/status
    web_node->getServer().Get("/api/calibration/status", [this](const httplib::Request&, httplib::Response& res) {
        CamCalStatus status = getLatestStatus();

        json j;
        j["camera_name"] = status.camera_name;
        j["session_id"] = status.session_id;
        j["cal_samples"] = status.cal_samples;
        j["reproj_err"] = status.reproj_err;
        j["min_x"] = status.min_x;
        j["max_x"] = status.max_x;
        j["min_y"] = status.min_y;
        j["max_y"] = status.max_y;
        j["max_skew"] = status.max_skew;
        j["max_scale"] = status.max_scale;

        res.set_content(j.dump(), "application/json");
    });

    // POST /api/calibration/start
    // Required body: { "camera_name", "board_type", "board_rows", "board_cols" } plus optional
    // edge/marker lengths, aruco_dict and legacy_pattern (see calibration_handler.hpp).
    // The image topic is derived as /<camera_name>/driver/image_raw.
    web_node->getServer().Post("/api/calibration/start", [this](const httplib::Request& req, httplib::Response& res) {
        auto request = std::make_shared<CamCalRequest::Request>();
        request->request_id = CamCalRequest::Request::REQ_START_CAL;

        try {
            json body = json::parse(req.body);
            if (!body.contains("camera_name") || !body["camera_name"].is_string()) {
                res.status = 400;
                res.set_content("{\"error\": \"Missing required field: camera_name\"}", "application/json");
                return;
            }
            request->camera_name = body["camera_name"].get<std::string>();

            // edge_length_mm is optional; defaults to 22.86 mm (0.02286 m).
            double edge_length_mm = 22.86;
            if (body.contains("edge_length_mm") && body["edge_length_mm"].is_number()) {
                edge_length_mm = body["edge_length_mm"].get<double>();
            }
            if (edge_length_mm <= 0.0) {
                res.status = 400;
                res.set_content("{\"error\": \"edge_length_mm must be greater than 0\"}", "application/json");
                return;
            }
            request->edge_len = static_cast<float>(edge_length_mm / 1000.0);

            // Board type and geometry. Only types are checked here; camera_cal_node
            // validates the geometry and reports problems as ERR_INVLD_BOARD.
            if (!body.contains("board_type") || !body["board_type"].is_string()) {
                res.status = 400;
                res.set_content("{\"error\": \"Missing required field: board_type\"}", "application/json");
                return;
            }
            const auto board_type = body["board_type"].get<std::string>();
            if (board_type == "chessboard") {
                request->board_type = CamCalRequest::Request::BOARD_CHESSBOARD;
            } else if (board_type == "charuco") {
                request->board_type = CamCalRequest::Request::BOARD_CHARUCO;
            } else {
                res.status = 400;
                res.set_content("{\"error\": \"board_type must be 'chessboard' or 'charuco'\"}", "application/json");
                return;
            }

            for (const char* field : { "board_rows", "board_cols" }) {
                if (!body.contains(field) || !body[field].is_number_unsigned() ||
                    body[field].get<uint64_t>() > std::numeric_limits<uint16_t>::max()) {
                    res.status = 400;
                    res.set_content(json{ { "error", std::string("Missing or invalid field: ") + field } }.dump(),
                                    "application/json");
                    return;
                }
            }
            request->board_rows = body["board_rows"].get<uint16_t>();
            request->board_cols = body["board_cols"].get<uint16_t>();

            if (body.contains("marker_length_mm") && body["marker_length_mm"].is_number()) {
                request->marker_len = static_cast<float>(body["marker_length_mm"].get<double>() / 1000.0);
            }
            if (body.contains("aruco_dict") && body["aruco_dict"].is_string()) {
                request->aruco_dict = body["aruco_dict"].get<std::string>();
            }
            if (body.contains("legacy_pattern") && body["legacy_pattern"].is_boolean()) {
                request->charuco_legacy_pattern = body["legacy_pattern"].get<bool>();
            }
        } catch (const std::exception& e) {
            RCLCPP_WARN(logger_, "Failed to parse start request body: %s", e.what());
            res.status = 400;
            res.set_content("{\"error\": \"Invalid JSON body\"}", "application/json");
            return;
        }

        // Validate the camera name against registered cameras.
        const auto known = getCamerasForCalibration();
        if (!known.empty() && std::find(known.begin(), known.end(), request->camera_name) == known.end()) {
            RCLCPP_WARN(logger_, "Unknown camera name: '%s'", request->camera_name.c_str());
            res.status = 400;
            res.set_content("{\"error\": \"Unknown camera name\"}", "application/json");
            return;
        }

        // Derive image topic from camera name.
        request->image_topic = imageTopicForCamera(request->camera_name);

        RCLCPP_INFO(logger_, "Calling REQ_START_CAL (topic='%s', camera='%s', board_type=%u, %ux%u, edge_len=%.5f m)",
                    request->image_topic.c_str(), request->camera_name.c_str(), request->board_type,
                    request->board_rows, request->board_cols, request->edge_len);

        auto response = callService(request);
        if (!response) {
            res.status = 504;
            res.set_content("{\"error\": \"Service call timed out or unavailable\"}", "application/json");
            return;
        }

        json j;
        j["err_code"] = response->err_code;
        j["err_msg"] = response->err_msg;
        j["session_id"] = response->session_id;
        res.status = (response->err_code == CamCalRequest::Response::ERR_OK) ? 200 : 409;
        res.set_content(j.dump(), "application/json");
    });

    // POST /api/calibration/stop
    web_node->getServer().Post("/api/calibration/stop", [this](const httplib::Request&, httplib::Response& res) {
        auto request = std::make_shared<CamCalRequest::Request>();
        request->request_id = CamCalRequest::Request::REQ_STOP_CAL;

        RCLCPP_INFO(logger_, "Calling REQ_STOP_CAL");

        auto response = callService(request);
        if (!response) {
            res.status = 504;
            res.set_content("{\"error\": \"Service call timed out or unavailable\"}", "application/json");
            return;
        }

        json j;
        j["err_code"] = response->err_code;
        j["err_msg"] = response->err_msg;
        res.status = (response->err_code == CamCalRequest::Response::ERR_OK) ? 200 : 409;
        res.set_content(j.dump(), "application/json");
    });

    // POST /api/calibration/complete
    web_node->getServer().Post("/api/calibration/complete", [this](const httplib::Request&, httplib::Response& res) {
        auto request = std::make_shared<CamCalRequest::Request>();
        request->request_id = CamCalRequest::Request::REQ_COMPLETE_CAL;

        RCLCPP_INFO(logger_, "Calling REQ_COMPLETE_CAL");

        auto response = callService(request);
        if (!response) {
            res.status = 504;
            res.set_content("{\"error\": \"Service call timed out or unavailable\"}", "application/json");
            return;
        }

        json j;
        j["err_code"] = response->err_code;
        j["err_msg"] = response->err_msg;
        res.status = (response->err_code == CamCalRequest::Response::ERR_OK) ? 200 : 409;
        res.set_content(j.dump(), "application/json");
    });

    RCLCPP_INFO(logger_,
                "CalibrationHandler initialised\n"
                "  service : %s\n"
                "  status  : %s",
                config_.service_name.c_str(), config_.status_topic.c_str());
}

// ---------------------------------------------------------------------------

CalibrationHandler::CamCalStatus CalibrationHandler::getLatestStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    return latest_status_;
}

// ---------------------------------------------------------------------------

std::string CalibrationHandler::imageTopicForCamera(const std::string& camera_name) {
    return "/" + camera_name + "/driver/image_raw";
}

// ---------------------------------------------------------------------------

std::vector<std::string> CalibrationHandler::getCamerasForCalibration() const {
    return cameras_;
}

// ---------------------------------------------------------------------------

bool CalibrationHandler::registerCamera(const std::string& camera_name) {
    if (std::find(cameras_.begin(), cameras_.end(), camera_name) != cameras_.end()) {
        return false;
    }
    cameras_.push_back(camera_name);
    RCLCPP_INFO(logger_, "Registered camera '%s' (topic: %s)", camera_name.c_str(),
                imageTopicForCamera(camera_name).c_str());
    return true;
}

// ---------------------------------------------------------------------------

bool CalibrationHandler::removeCamera(const std::string& camera_name) {
    auto it = std::find(cameras_.begin(), cameras_.end(), camera_name);
    if (it == cameras_.end()) {
        return false;
    }
    cameras_.erase(it);
    RCLCPP_INFO(logger_, "Removed camera '%s'", camera_name.c_str());
    return true;
}

// ---------------------------------------------------------------------------

std::shared_ptr<CalibrationHandler::CamCalRequest::Response> CalibrationHandler::callService(
    std::shared_ptr<CamCalRequest::Request> request) {
    // Check service availability with a short timeout (non-blocking so we can
    // return an error quickly if camera_cal_node is not running).
    if (!cal_client_->wait_for_service(std::chrono::seconds(2))) {
        RCLCPP_WARN(logger_, "Service '%s' is not available", config_.service_name.c_str());
        return nullptr;
    }

    auto future = cal_client_->async_send_request(request);

    // Spin the dedicated executor until the response arrives or we time out.
    const auto ret = client_executor_->spin_until_future_complete(future, config_.service_timeout);
    if (ret != rclcpp::FutureReturnCode::SUCCESS) {
        RCLCPP_ERROR(logger_, "Service call to '%s' did not complete in time (ret=%d)",
                     config_.service_name.c_str(), static_cast<int>(ret));
        return nullptr;
    }

    return future.get();
}

}  // namespace camera_web::calibration