#pragma once

#include <basin/web_core/web_node.hpp>
#include <calibration_msgs/msg/cam_cal_status.hpp>
#include <calibration_msgs/srv/cam_cal_request.hpp>
#include <chrono>
#include <memory>
#include <mutex>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <string>

namespace camera_web::calibration {

/// @brief Configuration for the CalibrationHandler component.
struct CalibrationHandlerConfig {
    /// ROS 2 service name for camera_cal/request
    std::string service_name{ "camera_cal/request" };
    /// ROS 2 topic name for camera_cal/status
    std::string status_topic{ "camera_cal/status" };
    /// Timeout when waiting for the calibration service response
    std::chrono::milliseconds service_timeout{ std::chrono::seconds(10) };
};

/// @brief Web API component that exposes the camera_cal_node service and status
///        via a REST interface.
///
/// Camera topics are derived automatically from the camera name using the
/// convention: /<camera_name>/driver/image_raw
///
/// REST endpoints
/// --------------
///   GET  /api/calibration/cameras
///       Returns the list of registered camera names as JSON.
///
///   GET  /api/calibration/status
///       Returns the most recently received CamCalStatus as JSON.
///
///   POST /api/calibration/start
///       Body (required JSON): { "camera_name": "...", "edge_length_mm": <float> }
///       edge_length_mm is optional and defaults to 22.86 mm (0.02286 m).
///       Derives the image topic as /<camera_name>/driver/image_raw and calls
///       REQ_START_CAL on the camera_cal/request service.
///
///   POST /api/calibration/stop
///       Calls REQ_STOP_CAL on the camera_cal/request service.
///
///   POST /api/calibration/complete
///       Calls REQ_COMPLETE_CAL on the camera_cal/request service.
///
/// Service calls are fully synchronous from the HTTP handler's perspective;
/// a dedicated SingleThreadedExecutor spins the service client's callback
/// group so that the main node executor is never blocked.
class CalibrationHandler {
   public:
    using CamCalRequest = calibration_msgs::srv::CamCalRequest;
    using CamCalStatus = calibration_msgs::msg::CamCalStatus;

    CalibrationHandler(std::shared_ptr<basin::web_core::WebNode> web_node, CalibrationHandlerConfig config = {});

    /// @brief Returns the latest calibration status snapshot (thread-safe).
    CamCalStatus getLatestStatus() const;

    std::vector<std::string> getCamerasForCalibration() const;

    /// @brief Derives the ROS 2 image topic for a given camera name.
    /// @param camera_name The camera name (e.g. "front_cam").
    /// @return Topic string of the form /<camera_name>/driver/image_raw.
    static std::string imageTopicForCamera(const std::string& camera_name);

    /// @brief Registers a camera name as available for calibration. This is used to populate the camera
    ///        selection dropdown in the UI and validate incoming requests.
    /// @param camera_name The name of the camera to register (e.g. "front_cam").
    /// @return true if the camera was added, false if it was already registered.
    bool registerCamera(const std::string& camera_name);

    /// @brief Removes a previously registered camera name.
    /// @param camera_name The name of the camera to remove (e.g. "front_cam").
    /// @return true if the camera was found and removed, false if it was not registered.
    bool removeCamera(const std::string& camera_name);

   private:
    /// @brief Send a CamCalRequest synchronously from an HTTP handler thread.
    /// @return shared_ptr to the response, or nullptr on timeout/error.
    std::shared_ptr<CamCalRequest::Response> callService(std::shared_ptr<CamCalRequest::Request> request);

    ///< List of registered camera names available for calibration (e.g. "front_cam", "rear_cam", etc.)
    std::vector<std::string> cameras_;

    // --- ROS 2 interfaces ---
    rclcpp::Client<CamCalRequest>::SharedPtr cal_client_;
    rclcpp::Subscription<CamCalStatus>::SharedPtr status_sub_;

    /// Dedicated executor for spinning just the service-client callback group.
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> client_executor_;
    rclcpp::CallbackGroup::SharedPtr client_cbg_;

    // --- Latest status (protected by mutex for cross-thread access) ---
    mutable std::mutex status_mutex_;
    CamCalStatus latest_status_{};

    CalibrationHandlerConfig config_;
    rclcpp::Logger logger_{ rclcpp::get_logger("CalibrationHandler") };
};

}  // namespace camera_web::calibration