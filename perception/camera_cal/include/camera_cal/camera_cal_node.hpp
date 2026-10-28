#pragma once

#include <atomic>
#include <mutex>
#include <thread>

#include <basin/node_core/node_core.hpp>
#include <calibration_msgs/msg/cam_cal_status.hpp>
#include <calibration_msgs/srv/cam_cal_request.hpp>
#include <image_transport/image_transport.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

#include "camera_cal/calibrator.hpp"
#include "camera_cal/camera_cal_params.hpp"

namespace camera_cal {

/// @brief ROS 2 component node that manages a camera calibration session.
///
/// Service
/// -------
///   camera_cal/request  (calibration_msgs/CalRequest)
///       REQ_START_CAL    – Begin a new calibration session for the supplied
///                          image_topic / camera_name.  board_type selects the
///                          calibrator (BOARD_CHESSBOARD or BOARD_CHARUCO), and
///                          board_rows / board_cols / edge_len (plus marker_len,
///                          aruco_dict and charuco_legacy_pattern for ChArUco)
///                          describe the board.  Rejected if a session is
///                          already active (ERR_CAL_RUNNING) or the board is
///                          invalid (ERR_INVLD_EDGE_LEN / ERR_INVLD_BOARD).
///       REQ_STOP_CAL     – Abort the current session and discard data.
///                          Rejected if no session is active (ERR_NO_CAL_RUNNING).
///       REQ_COMPLETE_CAL – Finalise calibration, save results to disk, and
///                          end the session.
///
/// Published topics
/// ----------------
///   camera_cal/status      (calibration_msgs/CalStatus)
///       Emitted every tick while a calibration session is active.
///
///   camera_cal/debug_image (sensor_msgs/Image)
///       The latest image annotated with detected corners (active session only).
class CameraCalNode : public basin::node_core::NodeCore {
   public:
    CameraCalNode() = delete;
    explicit CameraCalNode(const rclcpp::NodeOptions& options);

    /// @brief Auto-capture from incoming images during an active session.
    void imageCallback(const sensor_msgs::msg::Image::ConstSharedPtr& msg);

    // NodeCore virtual method implementations
    void onTick() override;
    void shutdown() override;

   private:
    // ----- ROS interfaces -----
    std::unique_ptr<CameraCalParams> params_;

    image_transport::Subscriber img_sub_;
    image_transport::Publisher debug_pub_;

    rclcpp::Service<calibration_msgs::srv::CamCalRequest>::SharedPtr cal_request_srv_;
    rclcpp::Publisher<calibration_msgs::msg::CamCalStatus>::SharedPtr status_pub_;

    // ----- Calibration session state -----
    std::atomic<bool> cal_active_{ false };
    /// True while the background calibration thread is running.
    std::atomic<bool> cal_computing_{ false };
    std::string active_camera_name_;
    std::string active_topic_name_;

    std::unique_ptr<CameraCalibrator> calibrator_;
    /// Background thread that runs cv::calibrateCamera so the service
    /// callback can return immediately without blocking the node monitor.
    std::thread cal_thread_;

    /// Size of the last received image (used as input to cv::calibrateCamera)
    cv::Size image_size_;

    /// Timestamp of the last frame that was actually processed (used to throttle to 1 Hz).
    rclcpp::Time last_frame_time_;

    /// RMS reprojection error from the most recently completed calibration.
    /// -1 while no calibration has completed yet or while computing.
    /// Written by the background thread, read by onTick.
    std::atomic<float> last_rms_{ -1.0f };

    /// Number of frames that were passed to cv::calibrateCamera in the last
    /// REQ_COMPLETE_CAL.  Frozen before the thread starts so onTick can
    /// display it while cal_computing_ is true.
    std::atomic<uint16_t> last_cal_samples_{ 0 };

    /// Unique identifier for the currently-active calibration session.
    /// Equals INACTIVE_SESSION (0) when no calibration is running.
    std::atomic<uint64_t> active_session_id_{ 0 };

    /// Counter used to generate unique, non-zero session IDs.
    uint64_t next_session_id_{ 0 };

    // ----- Callbacks -----
    void calRequestCallback(const calibration_msgs::srv::CamCalRequest::Request::SharedPtr req,
                            calibration_msgs::srv::CamCalRequest::Response::SharedPtr resp);

    // ----- Helpers -----
    /// Build the calibrator subclass selected by @p req.board_type.
    /// @return nullptr with @p err set if the board description is invalid
    std::unique_ptr<CameraCalibrator> makeCalibrator(const calibration_msgs::srv::CamCalRequest::Request& req,
                                                     std::string& err) const;

    /// Subscribe or re-subscribe img_sub_ to @p topic.
    void subscribeToTopic(const std::string& topic);

    /// Unsubscribe img_sub_.
    void unsubscribe();
};

}  // namespace camera_cal
