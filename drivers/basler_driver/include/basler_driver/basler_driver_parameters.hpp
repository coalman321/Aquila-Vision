#pragma once

#include <cstddef>
#include <rclcpp/node.hpp>
#include <string>

namespace basler_driver {

enum SHUTTER_MODE { SM_ROLLING = 0, SM_GLOBAL = 1, SM_GLOBAL_RESET_RELEASE = 2, SM_DEFAULT = -1 };

// The node's startup parameters: the same names, defaults and validation as pylon-ros-camera's
// BaslerDriverParameters, minus the GigE-only gige/* ones, auto_flash* (not on USB cameras) and
// enable_current_params_publisher (no current_params topic).
class BaslerDriverParameters {
   public:
    // Declares any parameter not already given as an override, then reads and validates them all.
    void readFromRosParameterServer(rclcpp::Node& nh);

    const std::string& deviceUserID() const { return device_user_id_; }
    void setDeviceUserId(rclcpp::Node& nh, const std::string& device_user_id);
    const std::string& deviceSerial() const { return device_serial_; }

    const std::string& imageEncoding() const { return image_encoding_; }
    void setimageEncodingParam(rclcpp::Node& nh, const std::string& format);

    const std::string& cameraFrame() const { return camera_frame_; }

    const double& frameRate() const { return frame_rate_; }
    void setFrameRate(rclcpp::Node& nh, const double& frame_rate);

    const std::string& cameraInfoURL() const { return camera_info_url_; }

    std::string shutterModeString() const;

    std::size_t binning_x_ = 1;
    std::size_t binning_y_ = 1;
    bool binning_x_given_ = false;
    bool binning_y_given_ = false;

    int downsampling_factor_exposure_search_ = 1;

    // Exposure time in microseconds.
    double exposure_ = 10000.0;
    bool exposure_given_ = false;

    // Gain as a fraction (0..1) of the camera's gain range.
    double gain_ = 0.5;
    bool gain_given_ = false;

    double gamma_ = 1.0;
    bool gamma_given_ = false;

    // Target mean image intensity, 0..255.
    int brightness_ = 100;
    bool brightness_given_ = false;
    bool brightness_continuous_ = false;
    bool exposure_auto_ = true;
    bool gain_auto_ = true;

    double exposure_search_timeout_ = 5.0;
    double auto_exposure_upper_limit_ = 0.0;

    bool enable_status_publisher_ = false;

    // "Default", "UserSet1".."UserSet3" or "CurrentSetting" (anything else is treated as the latter).
    std::string startup_user_set_;

    SHUTTER_MODE shutter_mode_ = SM_DEFAULT;

    // Milliseconds.
    int grab_timeout_ = 500;
    int trigger_timeout_ = 5000;

    int white_balance_auto_ = 0;
    double white_balance_ratio_red_ = 1.0;
    double white_balance_ratio_green_ = 1.0;
    double white_balance_ratio_blue_ = 1.0;

    // 0 = OneByOne, 1 = LatestImageOnly, 2 = LatestImages.
    int grab_strategy_ = 0;

   private:
    void validateParameterSet(rclcpp::Node& nh);

    std::string camera_frame_ = "basler_camera";
    std::string device_user_id_;
    std::string device_serial_;
    double frame_rate_ = 5.0;
    std::string camera_info_url_;
    std::string image_encoding_;
};

}  // namespace basler_driver
