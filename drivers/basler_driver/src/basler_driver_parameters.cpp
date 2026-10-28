#include "basler_driver/basler_driver_parameters.hpp"

#include <rclcpp/logging.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <type_traits>

namespace basler_driver {

namespace {

const rclcpp::Logger LOGGER = rclcpp::get_logger("basler_driver.parameters");

// Declares `name` (once; this runs again when the camera reconnects) and reads it. Returns whether it
// was given at launch, which several parameters use to tell "set by the user" from "defaulted".
template <typename T>
bool declareAndGet(rclcpp::Node& nh, const std::string& name, const T& default_value, T& value) {
    const auto& overrides = nh.get_node_parameters_interface()->get_parameter_overrides();
    const auto given = overrides.find(name);
    if (!nh.has_parameter(name)) {
        const bool given_as_integer =
            given != overrides.end() && given->second.get_type() == rclcpp::ParameterType::PARAMETER_INTEGER;
        if constexpr (std::is_same_v<T, double>) {
            // Launch files fill these from JSON, where 1000.0 easily becomes 1000: take integers too.
            if (given_as_integer) {
                nh.declare_parameter(name, rclcpp::ParameterValue(static_cast<double>(given->second.get<int64_t>())),
                                     rcl_interfaces::msg::ParameterDescriptor(), true);
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            // Basler serial numbers are all digits, so `-p device_serial:=40123456` arrives as an integer.
            if (given_as_integer) {
                nh.declare_parameter(name, rclcpp::ParameterValue(std::to_string(given->second.get<int64_t>())),
                                     rcl_interfaces::msg::ParameterDescriptor(), true);
            }
        }
        if (!nh.has_parameter(name)) {
            nh.declare_parameter<T>(name, default_value);
        }
    }
    nh.get_parameter(name, value);
    return given != overrides.end();
}

}  // namespace

void BaslerDriverParameters::readFromRosParameterServer(rclcpp::Node& nh) {
    declareAndGet<std::string>(nh, "camera_frame", "basler_camera", camera_frame_);
    declareAndGet<std::string>(nh, "device_user_id", "", device_user_id_);
    declareAndGet<std::string>(nh, "device_serial", "", device_serial_);
    declareAndGet<double>(nh, "frame_rate", 5.0, frame_rate_);
    declareAndGet<std::string>(nh, "camera_info_url", "", camera_info_url_);

    int binning_x = 1;
    declareAndGet<int>(nh, "binning_x", 1, binning_x);
    binning_x_given_ = true;
    if (binning_x > 32 || binning_x < 0) {
        RCLCPP_WARN_STREAM(LOGGER, "Specified binning_x factor not in valid range! Binning x = "
                                       << binning_x << ". Will reset it to default value (1)");
        binning_x = 1;
        binning_x_given_ = false;
    }
    binning_x_ = static_cast<std::size_t>(binning_x);

    int binning_y = 1;
    declareAndGet<int>(nh, "binning_y", 1, binning_y);
    binning_y_given_ = true;
    if (binning_y > 32 || binning_y < 0) {
        RCLCPP_WARN_STREAM(LOGGER, "Specified binning_y factor not in valid range! Binning y = "
                                       << binning_y << ". Will reset it to default value (1)");
        binning_y = 1;
        binning_y_given_ = false;
    }
    binning_y_ = static_cast<std::size_t>(binning_y);

    declareAndGet<int>(nh, "downsampling_factor_exposure_search", 20, downsampling_factor_exposure_search_);

    std::string encoding;
    declareAndGet<std::string>(nh, "image_encoding", "", encoding);
    namespace enc = sensor_msgs::image_encodings;
    if (!encoding.empty() && !enc::isMono(encoding) && !enc::isColor(encoding) && !enc::isBayer(encoding) &&
        encoding != enc::YUV422) {
        RCLCPP_WARN_STREAM(LOGGER, "Specified image encoding parameter: '"
                                       << encoding << "' is not part of the 'sensor_msgs/image_encodings.hpp' list!"
                                       << " Will not set encoding");
        encoding.clear();
    }
    image_encoding_ = encoding;

    exposure_given_ = declareAndGet<double>(nh, "exposure", 10000.0, exposure_);
    gain_given_ = declareAndGet<double>(nh, "gain", 0.5, gain_);
    gamma_given_ = declareAndGet<double>(nh, "gamma", 1.0, gamma_);
    declareAndGet<bool>(nh, "brightness_continuous", false, brightness_continuous_);
    declareAndGet<bool>(nh, "exposure_auto", true, exposure_auto_);
    declareAndGet<bool>(nh, "gain_auto", true, gain_auto_);
    brightness_given_ = declareAndGet<int>(nh, "brightness", 100, brightness_);

    // A fixed gain and exposure leave nothing to reach a brightness with; otherwise brightness wins
    // and turns the auto functions on.
    if (gain_given_ && exposure_given_) {
        if (brightness_given_) {
            RCLCPP_WARN_STREAM(LOGGER,
                               "Gain and exposure are specified as startup parameters and hence assumed to be "
                               "fixed! The specified brightness ("
                                   << brightness_ << ") can't be reached! "
                                   << "Brightness is going to be ignored by only setting gain and exposure.");
        }
        brightness_given_ = false;
    } else {
        brightness_continuous_ = true;
        nh.set_parameter(rclcpp::Parameter("brightness_continuous", brightness_continuous_));
        exposure_auto_ = true;
        nh.set_parameter(rclcpp::Parameter("exposure_auto", exposure_auto_));
        gain_auto_ = true;
        nh.set_parameter(rclcpp::Parameter("gain_auto", gain_auto_));
    }

    declareAndGet<double>(nh, "exposure_search_timeout", 5.0, exposure_search_timeout_);
    declareAndGet<double>(nh, "auto_exposure_upper_limit", 10000000.0, auto_exposure_upper_limit_);
    declareAndGet<bool>(nh, "enable_status_publisher", false, enable_status_publisher_);
    declareAndGet<std::string>(nh, "startup_user_set", "", startup_user_set_);

    std::string shutter_mode;
    declareAndGet<std::string>(nh, "shutter_mode", "", shutter_mode);
    if (shutter_mode == "rolling") {
        shutter_mode_ = SM_ROLLING;
    } else if (shutter_mode == "global") {
        shutter_mode_ = SM_GLOBAL;
    } else if (shutter_mode == "global_reset") {
        shutter_mode_ = SM_GLOBAL_RESET_RELEASE;
    } else {
        shutter_mode_ = SM_DEFAULT;
    }

    declareAndGet<int>(nh, "grab_timeout", 500, grab_timeout_);
    declareAndGet<int>(nh, "trigger_timeout", 5000, trigger_timeout_);
    declareAndGet<int>(nh, "white_balance_auto", 0, white_balance_auto_);
    declareAndGet<double>(nh, "white_balance_ratio_red", 1.0, white_balance_ratio_red_);
    declareAndGet<double>(nh, "white_balance_ratio_green", 1.0, white_balance_ratio_green_);
    declareAndGet<double>(nh, "white_balance_ratio_blue", 1.0, white_balance_ratio_blue_);
    declareAndGet<int>(nh, "grab_strategy", 0, grab_strategy_);

    validateParameterSet(nh);
}

void BaslerDriverParameters::validateParameterSet(rclcpp::Node& nh) {
    if (!device_user_id_.empty()) {
        RCLCPP_INFO_STREAM(
            LOGGER, "Trying to connect the camera device with the following device user id: " << device_user_id_);
    } else if (!device_serial_.empty()) {
        RCLCPP_INFO_STREAM(LOGGER, "Trying to connect the camera device with the following serial: " << device_serial_);
    } else {
        RCLCPP_INFO_STREAM(LOGGER, "No Device User ID set -> Will connect the first available camera device");
    }

    if (frame_rate_ < 0 && frame_rate_ != -1) {
        RCLCPP_WARN_STREAM(LOGGER, "The specified frame rate value - " << frame_rate_ << " Hz - is not valid!"
                                                                       << "-> Will reset it to default value (5 Hz).");
        setFrameRate(nh, 5.0);
    }
    if (exposure_given_ && (exposure_ <= 0.0 || exposure_ > 1e7)) {
        RCLCPP_WARN_STREAM(LOGGER, "The specified exposure value - " << exposure_ << " us - is out of valid range!"
                                                                     << "-> Will reset it to default value.");
        exposure_given_ = false;
    }
    if (gain_given_ && (gain_ < 0.0 || gain_ > 1.0)) {
        RCLCPP_WARN_STREAM(LOGGER, "The specified gain value - " << gain_ << " - is out of valid range (0 to 1)!"
                                                                 << "-> Will reset it to default value.");
        gain_given_ = false;
    }
    if (brightness_given_ && (brightness_ < 0 || brightness_ > 255)) {
        RCLCPP_WARN_STREAM(LOGGER, "The specified brightness value - " << brightness_
                                                                       << " - is out of valid range (0 to 255)!"
                                                                       << "-> Will reset it to default value.");
        brightness_given_ = false;
    }
    if (exposure_search_timeout_ < 5.0) {
        RCLCPP_WARN_STREAM(LOGGER, "The specified exposure search timeout value - " << exposure_search_timeout_
                                                                                    << " - is too low!"
                                                                                    << "-> Exposure search may fail.");
    }
}

void BaslerDriverParameters::setDeviceUserId(rclcpp::Node& nh, const std::string& device_user_id) {
    device_user_id_ = device_user_id;
    nh.set_parameter(rclcpp::Parameter("device_user_id", device_user_id_));
}

void BaslerDriverParameters::setimageEncodingParam(rclcpp::Node& nh, const std::string& format) {
    image_encoding_ = format;
    nh.set_parameter(rclcpp::Parameter("image_encoding", image_encoding_));
}

void BaslerDriverParameters::setFrameRate(rclcpp::Node& nh, const double& frame_rate) {
    frame_rate_ = frame_rate;
    nh.set_parameter(rclcpp::Parameter("frame_rate", frame_rate_));
}

std::string BaslerDriverParameters::shutterModeString() const {
    switch (shutter_mode_) {
        case SM_ROLLING:
            return "rolling";
        case SM_GLOBAL:
            return "global";
        case SM_GLOBAL_RESET_RELEASE:
            return "global_reset";
        default:
            return "default_shutter_mode";
    }
}

}  // namespace basler_driver
