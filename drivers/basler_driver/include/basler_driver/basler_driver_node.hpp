#pragma once

#include <array>
#include <chrono>
#include <basin/node_core/node_core.hpp>
#include <basler_driver_msgs/msg/component_status.hpp>
#include <camera_info_manager/camera_info_manager.hpp>
#include <cv_bridge/cv_bridge.hpp>
#include <diagnostic_updater/diagnostic_updater.hpp>
#include <functional>
#include <image_geometry/pinhole_camera_model.hpp>
#include <image_transport/image_transport.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <std_srvs/srv/trigger.hpp>
#include <string>
#include <vector>

#include "basler_driver/basler_camera.hpp"
#include "basler_driver/basler_driver_parameters.hpp"

namespace basler_driver {

// ROS 2 driver for Basler USB3 Vision cameras, through Aravis; modelled on pylon-ros-camera's node.
//
// Publishes ~/image_raw (+ camera_info), ~/image_rect when calibrated, and ~/status, as the original
// does. Camera settings are ROS parameters applied live with `ros2 param set`, instead of the
// original's set_* services; only commands remain services. See README.md.
class BaslerDriverNode : public basin::node_core::NodeCore {
   public:
    explicit BaslerDriverNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

    // NodeCore virtual method implementations
    void onTick() override;
    void shutdown() override;

   private:
    // A parameter backed directly by a GenICam feature. Declared only when the camera has the feature,
    // defaulting to the camera's current value.
    struct FeatureParameter {
        std::string name;
        // The first of these the camera has is used (Basler models name some features differently).
        std::vector<std::string> features;
        rclcpp::ParameterType type;
        std::string description;
        // Selector to set before the feature is accessed; empty if none.
        FeatureSelection selection;
        // Selects through the trigger_selector parameter rather than a fixed `selection`.
        bool trigger_selected = false;
        // Declare even when the feature can't be read right now (e.g. LineSource of a line that is
        // currently an input), with this default.
        std::optional<rclcpp::ParameterValue> fallback;
    };

    // Opens the camera and starts grabbing. False if it isn't there (onTick() retries) or can't be set up
    // (the node shuts down).
    bool connect();
    bool initAndRegister();
    bool startGrabbing();
    void spin();
    bool grabImage();

    void initServices();
    void declareNodeParameters();
    void declareFeatureParameters();
    // Pushes the camera's current values into the parameters after something (a user set, a .pfs
    // file) changed the camera behind their back.
    void refreshParametersFromCamera();
    void scheduleParameterRefresh();
    // At startup, applies the launch settings startGrabbing() doesn't; after a reconnect, replays
    // every setting made so far.
    void applyRemainingSettings(bool reconnect);

    rcl_interfaces::msg::SetParametersResult onParameterChange(const std::vector<rclcpp::Parameter>& parameters);
    // Applies one parameter to the camera: "done" or why it failed. `batch` holds the other values of
    // the same request, for settings made of several parameters (ROI, white balance, auto functions).
    std::string applyParameter(const rclcpp::Parameter& parameter, const std::vector<rclcpp::Parameter>& batch);
    // `name`'s value in `batch` if it is being changed there, else its current value.
    rclcpp::Parameter pendingValue(const std::string& name, const std::vector<rclcpp::Parameter>& batch) const;
    FeatureSelection selectionOf(const FeatureParameter& parameter) const;
    void rememberApplied(const std::string& name);

    bool setExposure(const float& target_exposure, float& reached_exposure);
    bool setGain(const float& target_gain, float& reached_gain);
    bool setGamma(const float& target_gamma, float& reached_gamma);
    bool setBrightness(const int& target_brightness, int& reached_brightness, const bool& exposure_auto,
                       const bool& gain_auto);
    bool setROI(const sensor_msgs::msg::RegionOfInterest& target_roi, sensor_msgs::msg::RegionOfInterest& reached_roi);
    bool setBinningX(const std::size_t& target_binning_x, std::size_t& reached_binning_x);
    bool setBinningY(const std::size_t& target_binning_y, std::size_t& reached_binning_y);
    void applyAutoFunctions(bool brightness_continuous, bool exposure_auto, bool gain_auto);
    std::string loadPfs(const std::string& file_name);
    // Restarts grabbing so stream settings (buffers, strategy, encoding) take effect.
    std::string restartGrabbing();
    void updateImageGeometry();
    // The timer that grabs and publishes a frame per tick.
    void resetFrameTimer(double frame_rate);

    void setupSamplingIndices(std::vector<std::size_t>& indices, std::size_t rows, std::size_t cols,
                              int downsampling_factor);
    void genSamplingIndicesRec(std::vector<std::size_t>& indices, const std::size_t& min_window_height,
                               const cv::Point2i& start, const cv::Point2i& end);
    float calcCurrentBrightness();
    void setupInitialCameraInfo(sensor_msgs::msg::CameraInfo& cam_info_msg);
    void setupRectification();
    uint32_t getNumSubscribersRectImagePub() const;
    void publishStatus(int8_t status_id, const std::string& message);

    void createDiagnostics(diagnostic_updater::DiagnosticStatusWrapper& stat);
    void createCameraInfoDiagnostics(diagnostic_updater::DiagnosticStatusWrapper& stat);

    std::unique_ptr<BaslerCamera> camera_;
    std::unique_ptr<BaslerDriverParameters> parameters_;
    std::shared_ptr<camera_info_manager::CameraInfoManager> camera_info_manager_;
    std::unique_ptr<image_geometry::PinholeCameraModel> pinhole_model_;
    std::unique_ptr<cv_bridge::CvImage> cv_bridge_img_rect_;

    image_transport::CameraPublisher img_raw_pub_;
    std::unique_ptr<image_transport::Publisher> img_rect_pub_;
    rclcpp::Publisher<basler_driver_msgs::msg::ComponentStatus>::SharedPtr component_status_pub_;
    std::vector<rclcpp::ServiceBase::SharedPtr> services_;
    rclcpp::TimerBase::SharedPtr timer_;
    rclcpp::TimerBase::SharedPtr refresh_timer_;
    diagnostic_updater::Updater diagnostics_updater_;
    rclcpp::node_interfaces::OnSetParametersCallbackHandle::SharedPtr param_callback_handle_;

    std::vector<FeatureParameter> feature_parameters_;
    bool feature_parameters_declared_ = false;
    std::chrono::steady_clock::time_point next_connect_attempt_;
    // Set by shutdown(), so onTick() leaves the released camera closed.
    bool shut_down_ = false;
    // Parameters to replay when the camera reconnects, in the order they were set.
    std::vector<std::string> applied_parameters_;
    // True while the node writes parameters itself: accept them without applying them to the camera.
    bool syncing_parameters_ = false;

    sensor_msgs::msg::Image img_raw_msg_;
    basler_driver_msgs::msg::ComponentStatus cm_status_;
    std::vector<std::size_t> sampling_indices_;
    // Exposure times that reached each brightness before, to start the next search from.
    std::array<float, 256> brightness_exp_lut_{};
    bool is_sleeping_ = false;

    std::recursive_mutex grab_mutex_;
};

}  // namespace basler_driver
