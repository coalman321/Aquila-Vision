#include "basler_driver/basler_driver_parameters.hpp"

#include <rclcpp/logging.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <type_traits>

namespace basler_driver {

namespace {

const rclcpp::Logger LOGGER = rclcpp::get_logger("basler_driver.parameters");

}  // namespace

template <typename T>
bool BaslerDriverParameters::createLaunchParam(const std::string& name, T& var) {
    const auto& overrides = node_->get_node_parameters_interface()->get_parameter_overrides();
    const auto given = overrides.find(name);
    const bool given_as_integer =
        given != overrides.end() && given->second.get_type() == rclcpp::ParameterType::PARAMETER_INTEGER;
    if (!node_->has_parameter(name) && given_as_integer) {
        if constexpr (std::is_same_v<T, double>) {
            // Launch files fill these from JSON, where 1000.0 easily becomes 1000: take integers too.
            node_->declare_parameter(name, rclcpp::ParameterValue(static_cast<double>(given->second.get<int64_t>())),
                                     rcl_interfaces::msg::ParameterDescriptor(), true);
        } else if constexpr (std::is_same_v<T, std::string>) {
            // Basler serial numbers are all digits, so `-p device_serial:=40123456` arrives as an integer.
            node_->declare_parameter(name, rclcpp::ParameterValue(std::to_string(given->second.get<int64_t>())),
                                     rcl_interfaces::msg::ParameterDescriptor(), true);
        }
    }
    createParam(name, var);
    return given != overrides.end();
}

void BaslerDriverParameters::readFromRosParameterServer() {
    createLaunchParam("camera_frame", camera_frame_);
    createLaunchParam("device_user_id", device_user_id_);
    createLaunchParam("device_serial", device_serial_);
    createLaunchParam("frame_rate", frame_rate_);
    createLaunchParam("camera_info_url", camera_info_url_);

    int binning_x = 1;
    createLaunchParam("binning_x", binning_x);
    binning_x_given_ = true;
    if (binning_x > 32 || binning_x < 0) {
        RCLCPP_WARN_STREAM(LOGGER, "Specified binning_x factor not in valid range! Binning x = "
                                       << binning_x << ". Will reset it to default value (1)");
        binning_x = 1;
        binning_x_given_ = false;
    }
    binning_x_ = static_cast<std::size_t>(binning_x);

    int binning_y = 1;
    createLaunchParam("binning_y", binning_y);
    binning_y_given_ = true;
    if (binning_y > 32 || binning_y < 0) {
        RCLCPP_WARN_STREAM(LOGGER, "Specified binning_y factor not in valid range! Binning y = "
                                       << binning_y << ". Will reset it to default value (1)");
        binning_y = 1;
        binning_y_given_ = false;
    }
    binning_y_ = static_cast<std::size_t>(binning_y);

    createLaunchParam("downsampling_factor_exposure_search", downsampling_factor_exposure_search_);

    std::string encoding;
    createLaunchParam("image_encoding", encoding);
    namespace enc = sensor_msgs::image_encodings;
    if (!encoding.empty() && !enc::isMono(encoding) && !enc::isColor(encoding) && !enc::isBayer(encoding) &&
        encoding != enc::YUV422) {
        RCLCPP_WARN_STREAM(LOGGER, "Specified image encoding parameter: '"
                                       << encoding << "' is not part of the 'sensor_msgs/image_encodings.hpp' list!"
                                       << " Will not set encoding");
        encoding.clear();
    }
    image_encoding_ = encoding;

    exposure_given_ = createLaunchParam("exposure", exposure_);
    gain_given_ = createLaunchParam("gain", gain_);
    gamma_given_ = createLaunchParam("gamma", gamma_);
    createLaunchParam("brightness_continuous", brightness_continuous_);
    createLaunchParam("exposure_auto", exposure_auto_);
    createLaunchParam("gain_auto", gain_auto_);
    brightness_given_ = createLaunchParam("brightness", brightness_);

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
        node_->set_parameter(rclcpp::Parameter("brightness_continuous", brightness_continuous_));
        exposure_auto_ = true;
        node_->set_parameter(rclcpp::Parameter("exposure_auto", exposure_auto_));
        gain_auto_ = true;
        node_->set_parameter(rclcpp::Parameter("gain_auto", gain_auto_));
    }

    createLaunchParam("exposure_search_timeout", exposure_search_timeout_);
    createLaunchParam("auto_exposure_upper_limit", auto_exposure_upper_limit_);
    createLaunchParam("enable_status_publisher", enable_status_publisher_);
    createLaunchParam("startup_user_set", startup_user_set_);

    std::string shutter_mode;
    createLaunchParam("shutter_mode", shutter_mode);
    if (shutter_mode == "rolling") {
        shutter_mode_ = SM_ROLLING;
    } else if (shutter_mode == "global") {
        shutter_mode_ = SM_GLOBAL;
    } else if (shutter_mode == "global_reset") {
        shutter_mode_ = SM_GLOBAL_RESET_RELEASE;
    } else {
        shutter_mode_ = SM_DEFAULT;
    }

    createLaunchParam("grab_timeout", grab_timeout_);
    createLaunchParam("trigger_timeout", trigger_timeout_);
    createLaunchParam("white_balance_auto", white_balance_auto_);
    createLaunchParam("white_balance_ratio_red", white_balance_ratio_red_);
    createLaunchParam("white_balance_ratio_green", white_balance_ratio_green_);
    createLaunchParam("white_balance_ratio_blue", white_balance_ratio_blue_);
    createLaunchParam("grab_strategy", grab_strategy_);

    validateParameterSet();
}

void BaslerDriverParameters::validateParameterSet() {
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
        setFrameRate(5.0);
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

void BaslerDriverParameters::setDeviceUserId(const std::string& device_user_id) {
    device_user_id_ = device_user_id;
    node_->set_parameter(rclcpp::Parameter("device_user_id", device_user_id_));
}

void BaslerDriverParameters::setimageEncodingParam(const std::string& format) {
    image_encoding_ = format;
    node_->set_parameter(rclcpp::Parameter("image_encoding", image_encoding_));
}

void BaslerDriverParameters::setFrameRate(const double& frame_rate) {
    frame_rate_ = frame_rate;
    node_->set_parameter(rclcpp::Parameter("frame_rate", frame_rate_));
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
