#include "camera_webserver/logger/logger_handler.hpp"

#include <algorithm>
#include <array>
#include <nlohmann/json.hpp>
#include <rclcpp/logging.hpp>
#include <regex>

using json = nlohmann::json;

namespace camera_web::logger {

namespace {

/// The look-back durations the UI offers, in seconds
constexpr std::array<int, 4> ALLOWED_DURATIONS_S{ 30, 60, 120, 300 };

/// Snapshot IDs name the logger's manifest files, so keep them to filename-safe characters
const std::regex SNAPSHOT_NAME_REGEX("^[A-Za-z0-9_-]{1,64}$");

const char* stateName(uint8_t state) {
    switch (state) {
        case logging_msgs::msg::SnapshotStatus::STATE_RUNNING:
            return "running";
        case logging_msgs::msg::SnapshotStatus::STATE_PENDING:
            return "pending";
        default:
            return "complete";
    }
}

json statusToJson(const logging_msgs::msg::LoggerStatus& status) {
    json j;
    j["connected"] = true;
    j["log_path"] = status.log_path;
    j["disk"] = {
        { "total_bytes", status.disk_total_bytes },
        { "free_bytes", status.disk_free_bytes },
        { "usage_percent", status.disk_usage_percent },
        { "rotation_threshold_percent", status.disk_rotation_threshold_percent },
        { "log_bytes", status.log_bytes },
        { "snapshot_protected_bytes", status.snapshot_protected_bytes },
        { "snapshot_usage_percent", status.snapshot_usage_percent },
        { "snapshot_usage_threshold_percent", status.snapshot_usage_threshold_percent },
    };

    j["snapshots"] = json::array();
    for (const auto& snapshot : status.snapshots) {
        j["snapshots"].push_back({
            { "id", snapshot.id },
            { "trigger", snapshot.trigger },
            { "description", snapshot.description },
            { "tags", snapshot.tags },
            { "start_time", snapshot.start_time },
            { "end_time", snapshot.end_time },
            { "state", stateName(snapshot.state) },
        });
    }
    j["total_snapshot_count"] = status.total_snapshot_count;

    j["events"] = json::array();
    for (const auto& event : status.events) {
        j["events"].push_back({
            { "event_name", event.event_name },
            { "time_before", event.time_before },
            { "time_after", event.time_after },
            { "trigger_count", event.trigger_count },
        });
    }
    return j;
}

void setError(httplib::Response& res, int status, const std::string& message) {
    res.status = status;
    res.set_content(json{ { "error", message } }.dump(), "application/json");
}

void setServiceResponse(httplib::Response& res, const logging_msgs::srv::SnapshotRequest::Response& response) {
    using Response = logging_msgs::srv::SnapshotRequest::Response;
    json j;
    j["resp_code"] = response.resp_code;
    j["message"] = response.response_msg;
    switch (response.resp_code) {
        case Response::RESPONSE_OK:
            res.status = 200;
            break;
        case Response::RESPONSE_SNAPSHOT_ACTIVE:
        case Response::RESPONSE_SNAPSHOT_EXISTS:
            res.status = 409;
            break;
        default:
            res.status = 400;
            break;
    }
    res.set_content(j.dump(), "application/json");
}

}  // namespace

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

LoggerHandler::LoggerHandler(std::shared_ptr<basin::web_core::WebNode> web_node, LoggerHandlerConfig config) :
    config_(config) {
    // Create a mutually-exclusive callback group so the dedicated executor
    // can spin the service client without conflicting with the main executor.
    client_cbg_ =
        web_node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive, false /* don't auto-add */);

    // Service client bound to the isolated callback group.
    snapshot_client_ =
        web_node->create_client<SnapshotRequest>(config_.snapshot_service, rclcpp::ServicesQoS(), client_cbg_);

    // Dedicated executor – only spins client_cbg_.
    client_executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
    client_executor_->add_callback_group(client_cbg_, web_node->get_node_base_interface());

    // Subscribe to the status topic (main executor processes this callback). Transient local to match the
    // logger's publisher, so the latest status arrives as soon as we connect.
    status_sub_ = web_node->create_subscription<LoggerStatus>(
        config_.status_topic, rclcpp::QoS(1).reliable().transient_local(),
        [this](const LoggerStatus::SharedPtr msg) {
            std::lock_guard<std::mutex> lock(status_mutex_);
            latest_status_ = *msg;
            latest_status_time_ = std::chrono::steady_clock::now();
        });

    // -----------------------------------------------------------------------
    // REST routes
    // -----------------------------------------------------------------------

    // GET /api/logger/status
    web_node->getServer().Get("/api/logger/status", [this](const httplib::Request&, httplib::Response& res) {
        std::optional<LoggerStatus> status = getLatestStatus();
        if (!status) {
            res.set_content(json{ { "connected", false } }.dump(), "application/json");
            return;
        }
        res.set_content(statusToJson(*status).dump(), "application/json");
    });

    // POST /api/logger/snapshot
    // Required body: { "name", "duration_s" } plus optional description and tags (see logger_handler.hpp).
    web_node->getServer().Post("/api/logger/snapshot", [this](const httplib::Request& req, httplib::Response& res) {
        auto request = std::make_shared<SnapshotRequest::Request>();
        request->request_id = SnapshotRequest::Request::REQUEST_START;

        try {
            json body = json::parse(req.body);
            if (!body.contains("name") || !body["name"].is_string() ||
                !std::regex_match(body["name"].get<std::string>(), SNAPSHOT_NAME_REGEX)) {
                setError(res, 400, "name must be 1-64 letters, digits, '_' or '-'");
                return;
            }
            request->snapshot_name = body["name"].get<std::string>();

            if (!body.contains("duration_s") || !body["duration_s"].is_number_integer() ||
                std::find(ALLOWED_DURATIONS_S.begin(), ALLOWED_DURATIONS_S.end(), body["duration_s"].get<int>()) ==
                    ALLOWED_DURATIONS_S.end()) {
                setError(res, 400, "duration_s must be 30, 60, 120 or 300");
                return;
            }
            request->time_before = body["duration_s"].get<int>();

            if (body.contains("description")) {
                if (!body["description"].is_string()) {
                    setError(res, 400, "description must be a string");
                    return;
                }
                request->description = body["description"].get<std::string>();
            }
            if (body.contains("tags")) {
                if (!body["tags"].is_array() ||
                    !std::all_of(body["tags"].begin(), body["tags"].end(), [](const json& t) { return t.is_string(); })) {
                    setError(res, 400, "tags must be an array of strings");
                    return;
                }
                request->tags = body["tags"].get<std::vector<std::string>>();
            }
        } catch (const std::exception& e) {
            RCLCPP_WARN(logger_, "Failed to parse snapshot request body: %s", e.what());
            setError(res, 400, "Invalid JSON body");
            return;
        }

        RCLCPP_INFO(logger_, "Requesting snapshot '%s' of the last %.0f s", request->snapshot_name.c_str(),
                    request->time_before);

        auto response = callService(request);
        if (!response) {
            setError(res, 504, "Logger service call timed out or unavailable");
            return;
        }

        setServiceResponse(res, *response);
    });

    // DELETE /api/logger/snapshot?id=<snapshot id>
    // Any existing ID is accepted, not just the names this API creates, as snapshots also come from events and
    // other clients. The logger rejects unknown IDs.
    web_node->getServer().Delete("/api/logger/snapshot", [this](const httplib::Request& req, httplib::Response& res) {
        std::string id = req.get_param_value("id");
        if (id.empty()) {
            setError(res, 400, "Missing required parameter: id");
            return;
        }

        auto request = std::make_shared<SnapshotRequest::Request>();
        request->request_id = SnapshotRequest::Request::REQUEST_REMOVE;
        request->snapshot_name = id;

        RCLCPP_INFO(logger_, "Requesting removal of snapshot '%s'", id.c_str());

        auto response = callService(request);
        if (!response) {
            setError(res, 504, "Logger service call timed out or unavailable");
            return;
        }

        setServiceResponse(res, *response);
        // An unknown snapshot is not found rather than a bad request
        if (response->resp_code == SnapshotRequest::Response::RESPONSE_INVALID_NAME) {
            res.status = 404;
        }
    });

    RCLCPP_INFO(logger_,
                "LoggerHandler initialised\n"
                "  service : %s\n"
                "  status  : %s",
                config_.snapshot_service.c_str(), config_.status_topic.c_str());
}

// ---------------------------------------------------------------------------

std::optional<LoggerHandler::LoggerStatus> LoggerHandler::getLatestStatus() const {
    std::lock_guard<std::mutex> lock(status_mutex_);
    if (!latest_status_ || std::chrono::steady_clock::now() - latest_status_time_ > config_.stale_after) {
        return std::nullopt;
    }
    return latest_status_;
}

// ---------------------------------------------------------------------------

std::shared_ptr<LoggerHandler::SnapshotRequest::Response> LoggerHandler::callService(
    std::shared_ptr<SnapshotRequest::Request> request) {
    // HTTP handlers run on several threads, but only one may spin the dedicated executor at a time.
    std::lock_guard<std::mutex> lock(service_mutex_);

    // Check service availability with a short timeout so we can return an
    // error quickly if the continuous logger is not running.
    if (!snapshot_client_->wait_for_service(std::chrono::seconds(2))) {
        RCLCPP_WARN(logger_, "Service '%s' is not available", config_.snapshot_service.c_str());
        return nullptr;
    }

    auto future = snapshot_client_->async_send_request(request);

    // Spin the dedicated executor until the response arrives or we time out.
    const auto ret = client_executor_->spin_until_future_complete(future, config_.service_timeout);
    if (ret != rclcpp::FutureReturnCode::SUCCESS) {
        RCLCPP_ERROR(logger_, "Service call to '%s' did not complete in time (ret=%d)",
                     config_.snapshot_service.c_str(), static_cast<int>(ret));
        snapshot_client_->remove_pending_request(future);
        return nullptr;
    }

    return future.get();
}

}  // namespace camera_web::logger
