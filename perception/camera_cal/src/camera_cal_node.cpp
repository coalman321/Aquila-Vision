#include "camera_cal/camera_cal_node.hpp"

#include <cv_bridge/cv_bridge.hpp>
#include <rclcpp_components/register_node_macro.hpp>
#include <iomanip>
#include <sstream>

#include "camera_cal/charuco_calibrator.hpp"
#include "camera_cal/chessboard_calibrator.hpp"

using namespace std::placeholders;
using CalRequest = calibration_msgs::srv::CamCalRequest;
using CalStatus = calibration_msgs::msg::CamCalStatus;

namespace camera_cal {

CameraCalNode::CameraCalNode(const rclcpp::NodeOptions& options) :
    basin::node_core::NodeCore("camera_cal_node", "", options) {
    params_ = std::make_unique<CameraCalParams>(this);

    // Image transport – subscriber created dynamically per session
    image_transport::ImageTransport it(std::shared_ptr<rclcpp::Node>(this, [](auto*) {}));

    debug_pub_ = it.advertise("camera_cal/debug_image", rclcpp::SystemDefaultsQoS().get_rmw_qos_profile());

    // Status publisher – emits CalStatus every tick while a session is active
    status_pub_ = create_publisher<CalStatus>("camera_cal/status", rclcpp::SystemDefaultsQoS());

    // ----------------------------------------------------------------
    // Service: camera_cal/request  (calibration_msgs/CalRequest)
    //
    //   REQ_START_CAL    – begin a new calibration session
    //   REQ_STOP_CAL     – abort and discard captured data
    //   REQ_COMPLETE_CAL – run calibration, save, and end session
    // ----------------------------------------------------------------
    cal_request_srv_ =
        create_service<CalRequest>("camera_cal/request", std::bind(&CameraCalNode::calRequestCallback, this, _1, _2));

    RCLCPP_INFO(get_logger(),
                "Camera calibration node started\n"
                "  min_captures        : %d\n"
                "  output_dir         : %s\n"
                "  motion_pos_thresh   : %.2f (fraction of board diagonal)\n"
                "  motion_scale_thresh : %.2f (relative span change)\n"
                "  Send REQ_START_CAL on 'camera_cal/request' to begin.",
                params_->min_captures,
                params_->output_dir.c_str(),
                params_->motion_position_threshold, params_->motion_scale_threshold);

    last_frame_time_ = get_clock()->now();
}

// ---------------------------------------------------------------------------

void CameraCalNode::calRequestCallback(const CalRequest::Request::SharedPtr req, CalRequest::Response::SharedPtr resp) {
    switch (req->request_id) {
        // ---- START -----------------------------------------------
        case CalRequest::Request::REQ_START_CAL: {
            if (cal_active_) {
                resp->err_code = CalRequest::Response::ERR_CAL_RUNNING;
                resp->err_msg = "Calibration already active for camera '" + active_camera_name_ + "' on topic '" +
                                active_topic_name_ + "'";
                RCLCPP_WARN(get_logger(), "%s", resp->err_msg.c_str());
                return;
            }

            // validate edge length
            if (req->edge_len <= 0.0f) {
                resp->err_code = CalRequest::Response::ERR_INVLD_EDGE_LEN;
                resp->err_msg = "Edge length must be greater than 0";
                RCLCPP_WARN(get_logger(), "%s", resp->err_msg.c_str());
                return;
            }

            std::string board_err;
            auto calibrator = makeCalibrator(*req, board_err);
            if (!calibrator) {
                resp->err_code = CalRequest::Response::ERR_INVLD_BOARD;
                resp->err_msg = "Invalid board: " + board_err;
                RCLCPP_WARN(get_logger(), "%s", resp->err_msg.c_str());
                return;
            }

            active_camera_name_ = req->camera_name;
            active_topic_name_ = req->image_topic;

            // Reset last-session status so the new session starts clean.
            last_rms_ = -1.0f;
            last_cal_samples_ = 0;

            calibrator_ = std::move(calibrator);
            calibrator_->setMotionThresholds(static_cast<float>(params_->motion_position_threshold),
                                             static_cast<float>(params_->motion_scale_threshold));

            subscribeToTopic(active_topic_name_);
            cal_active_ = true;

            const uint64_t new_session_id = ++next_session_id_;
            active_session_id_.store(new_session_id);
            resp->session_id = new_session_id;

            resp->err_code = CalRequest::Response::ERR_OK;
            std::ostringstream board_desc;
            board_desc << std::fixed << std::setprecision(4);
            if (req->board_type == CalRequest::Request::BOARD_CHARUCO) {
                board_desc << "charuco " << req->board_rows << "x" << req->board_cols << " " << req->aruco_dict
                           << " sq=" << req->edge_len << "m mk=" << req->marker_len << "m"
                           << (req->charuco_legacy_pattern ? " (legacy)" : "");
            } else {
                board_desc << "chessboard " << req->board_rows << "x" << req->board_cols << " sq=" << req->edge_len
                           << "m";
            }
            resp->err_msg = "Calibration started for camera '" + active_camera_name_ + "' on topic '" +
                            active_topic_name_ + "' with " + board_desc.str();
            RCLCPP_INFO(get_logger(), "%s", resp->err_msg.c_str());
            break;
        }

        // ---- STOP ------------------------------------------------
        case CalRequest::Request::REQ_STOP_CAL: {
            if (!cal_active_) {
                resp->err_code = CalRequest::Response::ERR_NO_CAL_RUNNING;
                resp->err_msg = "No calibration session is currently active";
                RCLCPP_WARN(get_logger(), "%s", resp->err_msg.c_str());
                return;
            }

            if (cal_computing_) {
                resp->err_code = CalRequest::Response::ERR_CAL_RUNNING;
                resp->err_msg = "Calibration is currently computing – wait for it to finish or restart the node";
                RCLCPP_WARN(get_logger(), "%s", resp->err_msg.c_str());
                return;
            }

            const int count = calibrator_->getCaptureCount();
            unsubscribe();
            calibrator_.reset();
            active_session_id_.store(CalRequest::Response::INACTIVE_SESSION);
            cal_active_ = false;

            resp->err_code = CalRequest::Response::ERR_OK;
            resp->err_msg = "Calibration stopped – " + std::to_string(count) + " frame(s) discarded";
            RCLCPP_INFO(get_logger(), "%s", resp->err_msg.c_str());
            break;
        }

        // ---- COMPLETE --------------------------------------------
        case CalRequest::Request::REQ_COMPLETE_CAL: {
            if (!cal_active_) {
                resp->err_code = CalRequest::Response::ERR_NO_CAL_RUNNING;
                resp->err_msg = "No calibration session is currently active";
                RCLCPP_WARN(get_logger(), "%s", resp->err_msg.c_str());
                return;
            }

            if (cal_computing_) {
                resp->err_code = CalRequest::Response::ERR_CAL_RUNNING;
                resp->err_msg = "Calibration is already computing in the background";
                RCLCPP_WARN(get_logger(), "%s", resp->err_msg.c_str());
                return;
            }

            const int count = calibrator_->getCaptureCount();
            if (count < params_->min_captures) {
                resp->err_code = CalRequest::Response::ERR_NOT_READY;
                resp->err_msg = "Insufficient captures: " + std::to_string(count) + " / " +
                                std::to_string(params_->min_captures) + " required";
                RCLCPP_WARN(get_logger(), "%s", resp->err_msg.c_str());
                return;
            }

            // Stop capturing new frames before handing the calibrator off to the thread.
            unsubscribe();
            cal_computing_ = true;

            // Snapshot values needed by the thread (avoids races on member variables).
            const cv::Size image_size = image_size_;
            const std::string camera_name = active_camera_name_;
            const std::string output_path = params_->output_dir;
            last_cal_samples_ = static_cast<uint16_t>(count);

            // Join any previous thread (should already be finished at this point).
            if (cal_thread_.joinable()) {
                cal_thread_.join();
            }

            RCLCPP_INFO(get_logger(), "Launching background calibration thread with %d frame(s)\u2026", count);

            cal_thread_ = std::thread([this, image_size, camera_name, output_path]() {
                cv::Mat camera_matrix, dist_coeffs;
                const double rms = calibrator_->calibrate(image_size, camera_matrix, dist_coeffs);

                if (rms < 0.0) {
                    RCLCPP_ERROR(get_logger(), "cv::calibrateCamera failed");
                } else {
                    const bool saved = calibrator_->saveCalibration(output_path, camera_name, camera_matrix,
                                                                    dist_coeffs, image_size, rms);
                    if (saved) {
                        RCLCPP_INFO(get_logger(), "Calibration complete \u2013 saved to %s  (RMS: %.4f px)",
                                    output_path.c_str(), rms);
                    } else {
                        RCLCPP_ERROR(get_logger(),
                                     "Calibration succeeded (RMS %.4f px) but failed to save to %s",
                                     rms, output_path.c_str());
                    }
                }

                // Store the result so onTick can surface it after the thread exits.
                last_rms_ = static_cast<float>(rms);

                // Release calibrator_ before clearing flags so onTick never
                // sees cal_computing_==false while calibrator_ is still live.
                calibrator_.reset();
                active_session_id_.store(CalRequest::Response::INACTIVE_SESSION);
                cal_computing_ = false;
                cal_active_ = false;
            });

            resp->err_code = CalRequest::Response::ERR_OK;
            resp->err_msg = "Calibration computing in background (" + std::to_string(count) +
                            " frames) \u2013 result will be logged when complete";
            RCLCPP_INFO(get_logger(), "%s", resp->err_msg.c_str());
            break;
        }

        // ---- UNKNOWN ---------------------------------------------
        default:
            resp->err_code = CalRequest::Response::ERR_UNKNOWN;
            resp->err_msg = "Unknown request_id: " + std::to_string(req->request_id);
            RCLCPP_WARN(get_logger(), "%s", resp->err_msg.c_str());
            break;
    }
}

// ---------------------------------------------------------------------------

std::unique_ptr<CameraCalibrator> CameraCalNode::makeCalibrator(const CalRequest::Request& req,
                                                                std::string& err) const {
    switch (req.board_type) {
        case CalRequest::Request::BOARD_CHESSBOARD: {
            const ChessboardCalibrator::Config config{ req.board_rows, req.board_cols, req.edge_len };
            if (const auto e = ChessboardCalibrator::validate(config)) {
                err = *e;
                return nullptr;
            }
            return std::make_unique<ChessboardCalibrator>(config);
        }
        case CalRequest::Request::BOARD_CHARUCO: {
            const CharucoCalibrator::Config config{ req.board_rows,  req.board_cols, req.edge_len,
                                                    req.marker_len,  req.aruco_dict, req.charuco_legacy_pattern };
            if (const auto e = CharucoCalibrator::validate(config)) {
                err = *e;
                return nullptr;
            }
            return std::make_unique<CharucoCalibrator>(config);
        }
        default:
            err = "Unknown board_type: " + std::to_string(req.board_type);
            return nullptr;
    }
}

// ---------------------------------------------------------------------------

void CameraCalNode::imageCallback(const sensor_msgs::msg::Image::ConstSharedPtr& msg) {
    if (!cal_active_ || !calibrator_) {
        return;
    }

    // Throttle to 1 frame per second.
    const rclcpp::Time now = this->get_clock()->now();
    if ((now - last_frame_time_).seconds() < 1.0) {
        return;
    }
    last_frame_time_ = now;

    cv::Mat image = cv_bridge::toCvShare(msg, "mono8")->image;
    image_size_ = image.size();

    RCLCPP_DEBUG(get_logger(), "Received image frame (size: %dx%d)", image_size_.width, image_size_.height);

    const bool found = calibrator_->detectAndCapture(image);

    if (found) {
        RCLCPP_INFO(get_logger(), "Board detected – total captures: %d", calibrator_->getCaptureCount());
    }

    if (debug_pub_.getNumSubscribers() > 0) {
        auto debug_msg = cv_bridge::CvImage(msg->header, "bgr8", calibrator_->getDebugImage()).toImageMsg();
        debug_pub_.publish(debug_msg);
    }
}

// ---------------------------------------------------------------------------

void CameraCalNode::onTick() {
    CalStatus status;

    // Always carry the last known camera name so clients see it
    // during and after calibration.  It is only replaced when REQ_START_CAL
    // sets it to the new session value.
    status.camera_name = active_camera_name_;
    status.session_id  = active_session_id_.load();

    if (!cal_active_ && !cal_computing_) {
        // Idle – show the result of the last completed (or failed) calibration.
        status.cal_samples = last_cal_samples_.load();
        status.reproj_err  = last_rms_.load();
        status_pub_->publish(status);
        return;
    }

    // calibrator_ is owned by the background thread while cal_computing_ is
    // true – skip any access to it until the thread finishes.
    if (cal_computing_) {
        status.cal_samples = last_cal_samples_.load();
        status.reproj_err  = -1.0f;
        status_pub_->publish(status);
        RCLCPP_DEBUG(get_logger(), "Calibration computing in background for [%s]", active_camera_name_.c_str());
        return;
    }

    const int count = calibrator_->getCaptureCount();

    status.cal_samples = static_cast<uint16_t>(count);
    status.reproj_err  = -1.0f;  // -1 until a completed calibration is run

    if (count > 0 && image_size_.area() > 0) {
        const auto cov = calibrator_->getCoverageStats(image_size_);
        status.min_x     = cov.min_x;
        status.max_x     = cov.max_x;
        status.min_y     = cov.min_y;
        status.max_y     = cov.max_y;
        status.max_skew  = cov.max_skew;
        status.max_scale = cov.max_scale;
    }

    status_pub_->publish(status);

    RCLCPP_DEBUG(get_logger(), "Cal active [%s / %s] – %d frame(s)", active_camera_name_.c_str(),
                 active_topic_name_.c_str(), count);
}

// ---------------------------------------------------------------------------

void CameraCalNode::shutdown() {
    RCLCPP_INFO(get_logger(), "Camera calibration node shutting down");
    unsubscribe();
    if (cal_thread_.joinable()) {
        RCLCPP_INFO(get_logger(), "Waiting for background calibration thread to finish\u2026");
        cal_thread_.join();
    }
}

// ---------------------------------------------------------------------------

void CameraCalNode::subscribeToTopic(const std::string& topic) {
    // Unsubscribe the old subscription first (if any)
    unsubscribe();

    image_transport::ImageTransport it(std::shared_ptr<rclcpp::Node>(this, [](auto*) {}));
    img_sub_ =
        it.subscribe(topic, rclcpp::SystemDefaultsQoS().get_rmw_qos_profile(), &CameraCalNode::imageCallback, this);

    RCLCPP_INFO(get_logger(), "Subscribed to image topic: %s", topic.c_str());
}

void CameraCalNode::unsubscribe() { img_sub_ = image_transport::Subscriber{}; }

}  // namespace camera_cal

RCLCPP_COMPONENTS_REGISTER_NODE(camera_cal::CameraCalNode)
