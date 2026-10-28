#include "basler_driver/basler_driver_node.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <rclcpp_components/register_node_macro.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <set>

namespace basler_driver {

namespace {

const rclcpp::Logger LOGGER = rclcpp::get_logger("basler_driver");

using ComponentStatus = basler_driver_msgs::msg::ComponentStatus;
using Trigger = std_srvs::srv::Trigger;

// Set at launch only: they say which camera this node drives and how it starts.
const std::set<std::string> IDENTITY_PARAMETERS = { "camera_frame", "device_user_id", "device_serial",
                                                    "camera_info_url", "startup_user_set" };

// Launch settings startGrabbing() does not apply itself, so applyRemainingSettings() does.
const std::set<std::string> LATE_PARAMETERS = {
    "roi.x_offset",
    "roi.y_offset",
    "roi.width",
    "roi.height",
    "white_balance_auto",
    "white_balance_ratio_red",
    "white_balance_ratio_green",
    "white_balance_ratio_blue",
    "sleeping",
    "max_num_buffer",
    "output_queue_size",
    "max_transfer_size",
    "pfs_file",
};

rcl_interfaces::msg::ParameterDescriptor describe(const std::string& description) {
    rcl_interfaces::msg::ParameterDescriptor descriptor;
    descriptor.description = description;
    return descriptor;
}

std::string joined(const std::vector<std::string>& values) {
    std::string result;
    for (const std::string& value : values) {
        result += (result.empty() ? "" : ", ") + value;
    }
    return result;
}

}  // namespace

BaslerDriverNode::BaslerDriverNode(const rclcpp::NodeOptions& options) :
    Node("basler_driver", options),
    camera_info_manager_(std::make_shared<camera_info_manager::CameraInfoManager>(
        get_node_base_interface(), get_node_services_interface(), get_node_logging_interface(), "camera", "",
        rclcpp::SystemDefaultsQoS())),
    diagnostics_updater_(this) {
    component_status_pub_ = create_publisher<ComponentStatus>("~/status", 5);
    img_raw_pub_ = image_transport::create_camera_publisher(*this, "~/image_raw", rclcpp::QoS(10));
    initServices();

    diagnostics_updater_.setHardwareID("none");
    diagnostics_updater_.add("camera_availability", this, &BaslerDriverNode::createDiagnostics);
    diagnostics_updater_.add("intrinsic_calibration", this, &BaslerDriverNode::createCameraInfoDiagnostics);

    declareNodeParameters();
    // Registered before init() so the parameter writes it makes are accepted; they are only applied
    // to the camera once it is grabbing.
    param_callback_handle_ =
        add_on_set_parameters_callback(std::bind(&BaslerDriverNode::onParameterChange, this, std::placeholders::_1));

    if (!init()) {
        return;
    }
    declareFeatureParameters();
    applyRemainingSettings(false);

    RCLCPP_INFO_STREAM(LOGGER, "Start image grabbing if node connects to topic with a spinning rate of: "
                                   << parameters_.frameRate() << " Hz");
    resetFrameTimer(parameters_.frameRate());
}

void BaslerDriverNode::declareNodeParameters() {
    declare_parameter("sleeping", false, describe("Stop publishing images while true."));
    declare_parameter("trigger_selector", std::string("FrameStart"),
                      describe("Trigger that the trigger_* parameters configure."));
    declare_parameter("user_set_selector", std::string("UserSet1"),
                      describe("User set that the save_user_set/load_user_set services act on."));
    declare_parameter("roi.x_offset", 0, describe("Region of interest, in sensor pixels."));
    declare_parameter("roi.y_offset", 0, describe("Region of interest, in sensor pixels."));
    declare_parameter("roi.width", 0, describe("Region of interest width; 0 for the full sensor."));
    declare_parameter("roi.height", 0, describe("Region of interest height; 0 for the full sensor."));
    declare_parameter("max_num_buffer", 10, describe("Number of frame buffers the stream allocates."));
    declare_parameter("output_queue_size", 1,
                      describe("Frames kept for grab_strategy 2 (LatestImages); at most max_num_buffer."));
    declare_parameter("max_transfer_size", 0,
                      describe("USB maximum transfer size in bytes; 0 for the Aravis default."));
    declare_parameter("pfs_file", std::string(""),
                      describe("Feature persistence (.pfs) file loaded into the camera when set."));
}

bool BaslerDriverNode::init() {
    parameters_.readFromRosParameterServer(*this);
    is_sleeping_ = get_parameter("sleeping").as_bool();

    if (!initAndRegister()) {
        RCLCPP_ERROR(LOGGER, "Error when trying to init and register. Shutting down now.");
        rclcpp::shutdown();
        return false;
    }
    if (!startGrabbing()) {
        RCLCPP_ERROR(LOGGER, "Error when trying to start grabbing. Shutting down now.");
        rclcpp::shutdown();
        return false;
    }
    return true;
}

void BaslerDriverNode::publishStatus(int8_t status_id, const std::string& message) {
    cm_status_.status_id = status_id;
    cm_status_.status_msg = message;
    if (parameters_.enable_status_publisher_) {
        component_status_pub_->publish(cm_status_);
    }
}

bool BaslerDriverNode::initAndRegister() {
    OpenByType open_by_type = OpenByType::FirstFound;
    std::string id;
    if (!parameters_.deviceUserID().empty()) {
        open_by_type = OpenByType::DeviceUserID;
        id = parameters_.deviceUserID();
    } else if (!parameters_.deviceSerial().empty()) {
        open_by_type = OpenByType::DeviceSerial;
        id = parameters_.deviceSerial();
    }

    camera_ = BaslerCamera::create(open_by_type, id);
    if (camera_ == nullptr) {
        publishStatus(ComponentStatus::ERROR, "No available camera");
        RCLCPP_WARN_STREAM(LOGGER, "Failed to connect camera device with id: "
                                       << id
                                       << ". Wait and retry to connect until the specified camera is available...");
        rclcpp::Time end = now() + rclcpp::Duration::from_seconds(15);
        rclcpp::Rate r(0.5);
        while (rclcpp::ok() && camera_ == nullptr) {
            camera_ = BaslerCamera::create(open_by_type, id);
            if (camera_ == nullptr) {
                RCLCPP_WARN_STREAM(LOGGER,
                                   "Failed to connect camera device with id: " << id << ". Trying again in a bit...");
            }
            if (now() > end) {
                RCLCPP_WARN_STREAM(LOGGER, "No available camera. Keep waiting and trying...");
                publishStatus(ComponentStatus::ERROR, "No available camera");
                end = now() + rclcpp::Duration::from_seconds(15);
            }
            r.sleep();
        }
    }
    if (!rclcpp::ok() || camera_ == nullptr) {
        return false;
    }
    if (parameters_.deviceUserID().empty()) {
        parameters_.setDeviceUserId(*this, camera_->deviceUserID());
    }
    publishStatus(ComponentStatus::RUNNING, "running");

    if (!camera_->applySoftwareTriggerConfiguration()) {
        publishStatus(ComponentStatus::ERROR, "Error while registering the camera configuration");
        return false;
    }
    if (!camera_->applyCamSpecificStartupSettings(parameters_)) {
        RCLCPP_ERROR(LOGGER, "Error while applying the user-specified startup settings to the camera!");
        publishStatus(ComponentStatus::ERROR, "Error while applying the user-specified startup settings");
        return false;
    }
    return true;
}

bool BaslerDriverNode::startGrabbing() {
    if (!camera_->startGrabbing(parameters_)) {
        return false;
    }
    img_raw_msg_.header.frame_id = parameters_.cameraFrame();
    updateImageGeometry();

    if (!camera_info_manager_->setCameraName(camera_->deviceUserID())) {
        RCLCPP_WARN_STREAM(LOGGER, "[" << camera_->deviceUserID() << "] name not valid for camera_info_manager");
    }
    sensor_msgs::msg::CameraInfo initial_cam_info;
    setupInitialCameraInfo(initial_cam_info);
    camera_info_manager_->setCameraInfo(initial_cam_info);

    const std::string& url = parameters_.cameraInfoURL();
    if (url.empty() || !camera_info_manager_->validateURL(url)) {
        RCLCPP_INFO_STREAM(LOGGER, "CameraInfoURL needed for rectification! ROS2-Param: '"
                                       << get_namespace() << "/camera_info_url' = '" << url << "' is invalid!");
        RCLCPP_DEBUG_STREAM(LOGGER, "CameraInfoURL should have following style: "
                                        << "'file:///full/path/to/local/file.yaml' or "
                                        << "'package://<package_name>/relative/path/to/file.yaml'");
        RCLCPP_WARN(LOGGER, "Will only provide distorted /image_raw images!");
    } else if (camera_info_manager_->loadCameraInfo(url)) {
        setupRectification();
        sensor_msgs::msg::CameraInfo cam_info = camera_info_manager_->getCameraInfo();
        cam_info.header.frame_id = img_raw_msg_.header.frame_id;
        camera_info_manager_->setCameraInfo(cam_info);
    } else {
        RCLCPP_WARN(LOGGER, "Will only provide distorted /image_raw images!");
    }

    auto& parameters = parameters_;
    if (parameters.binning_x_given_) {
        std::size_t reached_binning_x = 0;
        setBinningX(parameters.binning_x_, reached_binning_x);
        RCLCPP_INFO_STREAM(LOGGER, "Setting horizontal binning_x to " << parameters.binning_x_);
    }
    if (parameters.binning_y_given_) {
        std::size_t reached_binning_y = 0;
        setBinningY(parameters.binning_y_, reached_binning_y);
        RCLCPP_INFO_STREAM(LOGGER, "Setting vertical binning_y to " << parameters.binning_y_);
    }
    if (parameters.exposure_given_) {
        float reached_exposure = 0.0f;
        setExposure(static_cast<float>(parameters.exposure_), reached_exposure);
        RCLCPP_INFO_STREAM(
            LOGGER, "Attempted to set exposure to " << parameters.exposure_ << ", reached: " << reached_exposure);
    }
    if (parameters.gain_given_) {
        float reached_gain = 0.0f;
        setGain(static_cast<float>(parameters.gain_), reached_gain);
        RCLCPP_INFO_STREAM(LOGGER, "Attempted to set gain to: " << parameters.gain_ << ", reached: " << reached_gain);
    }
    if (parameters.gamma_given_) {
        float reached_gamma = 0.0f;
        setGamma(static_cast<float>(parameters.gamma_), reached_gamma);
        RCLCPP_INFO_STREAM(LOGGER, "Attempted to set gamma to " << parameters.gamma_ << ", reached: " << reached_gamma);
    }
    if (parameters.brightness_given_) {
        int reached_brightness = 0;
        setBrightness(parameters.brightness_, reached_brightness, parameters.exposure_auto_, parameters.gain_auto_);
        RCLCPP_INFO_STREAM(LOGGER, "Attempted to set brightness to: " << parameters.brightness_
                                                                      << ", reached: " << reached_brightness);
        applyAutoFunctions(parameters.brightness_continuous_, parameters.exposure_auto_, parameters.gain_auto_);
    }

    RCLCPP_INFO_STREAM(LOGGER, "Startup settings: " << "encoding = '" << camera_->currentROSEncoding() << "', "
                                                    << "binning = [" << camera_->currentBinningX() << ", "
                                                    << camera_->currentBinningY() << "], "
                                                    << "exposure = " << camera_->currentExposure() << ", "
                                                    << "gain = " << camera_->currentGain() << ", "
                                                    << "gamma = " << camera_->currentGamma() << ", "
                                                    << "shutter mode = " << parameters.shutterModeString());

    const double max_frame_rate = camera_->maxPossibleFramerate();
    if (max_frame_rate < parameters.frameRate()) {
        RCLCPP_INFO(LOGGER, "Desired framerate %.2f is higher than max possible. Will limit framerate to: %.2f Hz",
                    parameters.frameRate(), max_frame_rate);
        parameters.setFrameRate(*this, max_frame_rate);
    } else if (parameters.frameRate() == -1) {
        parameters.setFrameRate(*this, max_frame_rate);
        RCLCPP_INFO(LOGGER, "Max possible framerate is %.2f Hz", max_frame_rate);
    }
    return true;
}

void BaslerDriverNode::resetFrameTimer(double frame_rate) {
    timer_ = create_wall_timer(std::chrono::duration<double>(1.0 / std::max(frame_rate, 0.1)),
                               std::bind(&BaslerDriverNode::spin, this));
}

void BaslerDriverNode::spin() {
    if (camera_info_manager_->isCalibrated()) {
        RCLCPP_INFO_ONCE(LOGGER, "Camera is calibrated");
    } else {
        RCLCPP_INFO_ONCE(LOGGER, "Camera not calibrated");
    }

    if (camera_->isCamRemoved()) {
        RCLCPP_ERROR(LOGGER, "Camera has been removed, trying to reset");
        publishStatus(ComponentStatus::ERROR, "Camera has been removed, trying to reset");
        {
            std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
            camera_.reset();
        }
        rclcpp::Rate(0.5).sleep();
        if (init()) {
            applyRemainingSettings(true);
        }
        return;
    }

    const bool any_subscriber = img_raw_pub_.getNumSubscribers() != 0 || getNumSubscribersRectImagePub() != 0;
    if (!is_sleeping_ && any_subscriber) {
        if (!grabImage()) {
            return;
        }
        if (img_raw_pub_.getNumSubscribers() > 0) {
            sensor_msgs::msg::CameraInfo cam_info = camera_info_manager_->getCameraInfo();
            cam_info.header.stamp = img_raw_msg_.header.stamp;
            img_raw_pub_.publish(img_raw_msg_, cam_info);
        }
        if (getNumSubscribersRectImagePub() > 0) {
            cv_bridge_img_rect_->header.stamp = img_raw_msg_.header.stamp;
            const int bit_depth = sensor_msgs::image_encodings::bitDepth(img_raw_msg_.encoding);
            std::string rect_encoding = img_raw_msg_.encoding;
            if (sensor_msgs::image_encodings::isBayer(rect_encoding)) {
                rect_encoding = bit_depth == 8 ? "bgr8" : "bgr16";
            }
            cv_bridge_img_rect_->encoding = rect_encoding;
            cv_bridge::CvImagePtr cv_img_raw = cv_bridge::toCvCopy(img_raw_msg_, rect_encoding);
            if (cv_img_raw == nullptr) {
                RCLCPP_ERROR(LOGGER, "Failed to initialize rectified image, not publishing it");
            } else {
                pinhole_model_->fromCameraInfo(camera_info_manager_->getCameraInfo());
                pinhole_model_->rectifyImage(cv_img_raw->image, cv_bridge_img_rect_->image);
                img_rect_pub_->publish(cv_bridge_img_rect_->toImageMsg());
            }
        }
    }

    // The camera fell back to another encoding (or a user set changed it): make the parameter say so.
    if (parameters_.imageEncoding() != camera_->currentROSEncoding()) {
        syncing_parameters_ = true;
        parameters_.setimageEncodingParam(*this, camera_->currentROSEncoding());
        syncing_parameters_ = false;
    }
    if (parameters_.enable_status_publisher_) {
        component_status_pub_->publish(cm_status_);
    }
}

bool BaslerDriverNode::grabImage() {
    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    rclcpp::Time stamp = now();
    if (!camera_->grab(img_raw_msg_.data, stamp)) {
        return false;
    }
    img_raw_msg_.header.stamp = stamp;
    return true;
}

void BaslerDriverNode::updateImageGeometry() {
    img_raw_msg_.encoding = camera_->currentROSEncoding();
    img_raw_msg_.height = static_cast<uint32_t>(camera_->imageRows());
    img_raw_msg_.width = static_cast<uint32_t>(camera_->imageCols());
    img_raw_msg_.step = img_raw_msg_.width * static_cast<uint32_t>(camera_->imagePixelDepth());
    setupSamplingIndices(sampling_indices_, camera_->imageRows(), camera_->imageCols(),
                         parameters_.downsampling_factor_exposure_search_);
}

std::string BaslerDriverNode::restartGrabbing() {
    camera_->grabbingStopping();
    const std::string result = camera_->grabbingStarting();
    updateImageGeometry();
    return result;
}

void BaslerDriverNode::initServices() {
    const auto command = [this](const std::string& name, std::function<std::string()> run) {
        services_.push_back(create_service<Trigger>(
            "~/" + name,
            [this, run](const std::shared_ptr<Trigger::Request>, std::shared_ptr<Trigger::Response> response) {
                {
                    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
                    if (!camera_ || !camera_->isReady()) {
                        response->success = false;
                        response->message = "camera is not ready!";
                        return;
                    }
                    response->message = run();
                }
                response->success = response->message.find("done") != std::string::npos;
                if (response->message == "Node is not writable.") {
                    response->message = "Using this feature requires stopping image grabbing";
                }
            }));
    };
    command("execute_software_trigger", [this] { return camera_->executeSoftwareTrigger(); });
    command("save_user_set", [this] { return camera_->saveUserSet(get_parameter("user_set_selector").as_string()); });
    command("load_user_set", [this] {
        const std::string result = camera_->loadUserSet(get_parameter("user_set_selector").as_string());
        updateImageGeometry();
        scheduleParameterRefresh();
        return result;
    });
    command("reset_device", [this] { return camera_->triggerDeviceReset(); });
    command("start_grabbing", [this] {
        const std::string result = camera_->grabbingStarting();
        updateImageGeometry();
        return result;
    });
    command("stop_grabbing", [this] { return camera_->grabbingStopping(); });
}

std::string BaslerDriverNode::loadPfs(const std::string& file_name) {
    if (file_name.empty()) {
        return "done";
    }
    const std::string result = camera_->loadPfs(file_name);
    updateImageGeometry();
    scheduleParameterRefresh();
    return result;
}

void BaslerDriverNode::declareFeatureParameters() {
    if (feature_parameters_declared_) {
        return;
    }
    using rclcpp::ParameterType;
    std::vector<FeatureParameter> candidates = {
        { "reverse_x", { "ReverseX" }, ParameterType::PARAMETER_BOOL, "Mirror the image horizontally." },
        { "reverse_y", { "ReverseY" }, ParameterType::PARAMETER_BOOL, "Mirror the image vertically." },
        { "black_level", { "BlackLevel" }, ParameterType::PARAMETER_DOUBLE, "Black level (offset) of the pixels." },
        { "pgi_mode", { "PgiMode" }, ParameterType::PARAMETER_BOOL, "Basler PGI image enhancement." },
        { "demosaicing_mode", { "DemosaicingMode" }, ParameterType::PARAMETER_STRING, "Demosaicing algorithm." },
        { "noise_reduction",
          { "NoiseReduction", "BslNoiseReduction" },
          ParameterType::PARAMETER_DOUBLE,
          "PGI noise reduction." },
        { "sharpness_enhancement",
          { "SharpnessEnhancement", "BslSharpnessEnhancement" },
          ParameterType::PARAMETER_DOUBLE,
          "PGI sharpness enhancement." },
        { "light_source_preset",
          { "LightSourcePreset", "BslLightSourcePreset" },
          ParameterType::PARAMETER_STRING,
          "Color correction for the light source." },
        { "sensor_readout_mode", { "SensorReadoutMode" }, ParameterType::PARAMETER_STRING, "Sensor readout mode." },
        { "acquisition_frame_count",
          { "AcquisitionBurstFrameCount" },
          ParameterType::PARAMETER_INTEGER,
          "Frames per FrameBurstStart trigger." },
        { "user_set_default",
          { "UserSetDefault", "UserSetDefaultSelector" },
          ParameterType::PARAMETER_STRING,
          "User set the camera loads at power-up." },
        { "device_link_throughput_limit_mode",
          { "DeviceLinkThroughputLimitMode" },
          ParameterType::PARAMETER_BOOL,
          "Limit the USB bandwidth the camera uses." },
        { "device_link_throughput_limit",
          { "DeviceLinkThroughputLimit" },
          ParameterType::PARAMETER_INTEGER,
          "USB bandwidth limit in bytes per second." },
        { "chunk_mode_active",
          { "ChunkModeActive" },
          ParameterType::PARAMETER_BOOL,
          "Append chunk data to frames. With chunks.Timestamp on, images are stamped with the camera's clock." },
    };
    for (const auto& [name, feature, type, description] :
         std::vector<std::tuple<std::string, std::string, ParameterType, std::string>>{
             { "trigger_mode", "TriggerMode", ParameterType::PARAMETER_BOOL,
               "Frames wait for a trigger (the driver fires software triggers at frame_rate)." },
             { "trigger_source", "TriggerSource", ParameterType::PARAMETER_STRING, "Signal that fires the trigger." },
             { "trigger_activation", "TriggerActivation", ParameterType::PARAMETER_STRING,
               "Edge of a hardware trigger signal." },
             { "trigger_delay", "TriggerDelay", ParameterType::PARAMETER_DOUBLE,
               "Delay from trigger to exposure, in microseconds." },
         }) {
        FeatureParameter parameter{ name, { feature }, type, description + " Applies to trigger_selector." };
        parameter.trigger_selected = true;
        candidates.push_back(parameter);
    }
    for (const std::string& line : camera_->featureEntries("LineSelector")) {
        const FeatureSelection selection{ "LineSelector", line };
        const std::string prefix = "lines." + line + ".";
        candidates.push_back(
            { prefix + "mode", { "LineMode" }, ParameterType::PARAMETER_STRING, "Input or Output.", selection });
        candidates.push_back({ prefix + "source",
                               { "LineSource" },
                               ParameterType::PARAMETER_STRING,
                               "Signal an output line carries.",
                               selection,
                               false,
                               rclcpp::ParameterValue("") });
        candidates.push_back({ prefix + "inverter",
                               { "LineInverter" },
                               ParameterType::PARAMETER_BOOL,
                               "Invert the line's signal.",
                               selection });
        candidates.push_back({ prefix + "debouncer_time",
                               { "LineDebouncerTime" },
                               ParameterType::PARAMETER_DOUBLE,
                               "Input debouncer, in microseconds.",
                               selection,
                               false,
                               rclcpp::ParameterValue(0.0) });
    }
    for (const std::string& output : camera_->featureEntries("UserOutputSelector")) {
        candidates.push_back({ "user_outputs." + output,
                               { "UserOutputValue" },
                               ParameterType::PARAMETER_BOOL,
                               "Level of the user output (when a line's source is " + output + ").",
                               FeatureSelection{ "UserOutputSelector", output } });
    }
    for (const std::string& timer : camera_->featureEntries("TimerSelector")) {
        const FeatureSelection selection{ "TimerSelector", timer };
        candidates.push_back({ "timers." + timer + ".duration",
                               { "TimerDuration" },
                               ParameterType::PARAMETER_DOUBLE,
                               "Timer duration, in microseconds.",
                               selection });
        candidates.push_back({ "timers." + timer + ".trigger_source",
                               { "TimerTriggerSource" },
                               ParameterType::PARAMETER_STRING,
                               "Signal that starts the timer.",
                               selection });
    }
    for (const std::string& chunk : camera_->featureEntries("ChunkSelector")) {
        candidates.push_back({ "chunks." + chunk,
                               { "ChunkEnable" },
                               ParameterType::PARAMETER_BOOL,
                               "Include the " + chunk + " chunk when chunk_mode_active.",
                               FeatureSelection{ "ChunkSelector", chunk } });
    }

    const auto& overrides = get_node_parameters_interface()->get_parameter_overrides();
    std::vector<std::string> given;
    syncing_parameters_ = true;
    for (FeatureParameter& parameter : candidates) {
        std::optional<rclcpp::ParameterValue> value;
        std::string feature = parameter.features.front();
        for (const std::string& candidate : parameter.features) {
            value = camera_->getFeature(candidate, parameter.type, selectionOf(parameter));
            if (value) {
                feature = candidate;
                break;
            }
        }
        if (!value) {
            if (!parameter.fallback || !camera_->hasFeature(feature)) {
                continue;
            }
            value = parameter.fallback;
        }
        parameter.features = { feature };
        auto descriptor = describe(parameter.description);
        if (parameter.type == ParameterType::PARAMETER_STRING) {
            if (const auto entries = camera_->featureEntries(feature); !entries.empty()) {
                descriptor.description += " One of: " + joined(entries) + ".";
            }
        }
        try {
            declare_parameter(parameter.name, *value, descriptor);
        } catch (const rclcpp::exceptions::InvalidParameterTypeException& e) {
            RCLCPP_WARN_STREAM(LOGGER, "Ignoring parameter " << parameter.name << ": " << e.what());
            declare_parameter(parameter.name, *value, descriptor, true);
        }
        feature_parameters_.push_back(parameter);
        if (overrides.count(parameter.name) > 0) {
            given.push_back(parameter.name);
        }
    }
    syncing_parameters_ = false;
    feature_parameters_declared_ = true;

    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    for (const std::string& name : given) {
        const std::string result = applyParameter(get_parameter(name), {});
        if (result == "done") {
            rememberApplied(name);
        } else {
            RCLCPP_WARN_STREAM(LOGGER, "Unable to apply " << name << ": " << result);
        }
    }
}

void BaslerDriverNode::applyRemainingSettings(bool reconnect) {
    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    if (reconnect) {
        for (const std::string& name : applied_parameters_) {
            const std::string result = applyParameter(get_parameter(name), {});
            if (result != "done") {
                RCLCPP_WARN_STREAM(LOGGER, "Unable to re-apply " << name << ": " << result);
            }
        }
        return;
    }
    for (const auto& [name, value] : get_node_parameters_interface()->get_parameter_overrides()) {
        if (!has_parameter(name)) {
            RCLCPP_WARN_STREAM(LOGGER, "Ignoring parameter " << name << ": not a setting of this camera");
            continue;
        }
        if (LATE_PARAMETERS.count(name) == 0) {
            continue;
        }
        const std::string result = applyParameter(get_parameter(name), {});
        if (result == "done") {
            rememberApplied(name);
        } else {
            RCLCPP_WARN_STREAM(LOGGER, "Unable to apply " << name << ": " << result);
        }
    }
}

void BaslerDriverNode::refreshParametersFromCamera() {
    std::vector<rclcpp::Parameter> updates;
    for (const FeatureParameter& parameter : feature_parameters_) {
        if (auto value = camera_->getFeature(parameter.features.front(), parameter.type, selectionOf(parameter))) {
            updates.emplace_back(parameter.name, *value);
        }
    }
    updates.emplace_back("exposure", static_cast<double>(camera_->currentExposure()));
    updates.emplace_back("gain", static_cast<double>(camera_->currentGain()));
    if (const float gamma = camera_->currentGamma(); gamma > -10000.0f) {
        updates.emplace_back("gamma", static_cast<double>(gamma));
    }
    updates.emplace_back("binning_x", static_cast<int64_t>(camera_->currentBinningX()));
    updates.emplace_back("binning_y", static_cast<int64_t>(camera_->currentBinningY()));
    const sensor_msgs::msg::RegionOfInterest roi = camera_->currentROI();
    updates.emplace_back("roi.x_offset", static_cast<int64_t>(roi.x_offset));
    updates.emplace_back("roi.y_offset", static_cast<int64_t>(roi.y_offset));
    updates.emplace_back("roi.width", static_cast<int64_t>(roi.width));
    updates.emplace_back("roi.height", static_cast<int64_t>(roi.height));

    syncing_parameters_ = true;
    for (const auto& result : set_parameters(updates)) {
        if (!result.successful) {
            RCLCPP_WARN_STREAM(LOGGER, "Unable to refresh a parameter from the camera: " << result.reason);
        }
    }
    parameters_.setimageEncodingParam(*this, camera_->currentROSEncoding());
    syncing_parameters_ = false;
}

void BaslerDriverNode::scheduleParameterRefresh() {
    // Parameters can't be set from inside the parameter callback (pfs_file), so refresh on the next spin.
    refresh_timer_ = create_wall_timer(std::chrono::milliseconds(1), [this] {
        refresh_timer_->cancel();
        std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
        if (camera_ && camera_->isReady()) {
            refreshParametersFromCamera();
        }
    });
}

FeatureSelection BaslerDriverNode::selectionOf(const FeatureParameter& parameter) const {
    if (parameter.trigger_selected) {
        return { "TriggerSelector", get_parameter("trigger_selector").as_string() };
    }
    return parameter.selection;
}

void BaslerDriverNode::rememberApplied(const std::string& name) {
    applied_parameters_.erase(std::remove(applied_parameters_.begin(), applied_parameters_.end(), name),
                              applied_parameters_.end());
    applied_parameters_.push_back(name);
}

rclcpp::Parameter BaslerDriverNode::pendingValue(const std::string& name,
                                                 const std::vector<rclcpp::Parameter>& batch) const {
    for (const rclcpp::Parameter& parameter : batch) {
        if (parameter.get_name() == name) {
            return parameter;
        }
    }
    return get_parameter(name);
}

rcl_interfaces::msg::SetParametersResult BaslerDriverNode::onParameterChange(
    const std::vector<rclcpp::Parameter>& parameters) {
    rcl_interfaces::msg::SetParametersResult result;
    result.successful = true;
    if (syncing_parameters_) {
        return result;
    }
    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    if (!camera_ || !camera_->isReady()) {
        // Starting up (or reconnecting): init() applies the values once the camera is grabbing.
        return result;
    }
    for (const rclcpp::Parameter& parameter : parameters) {
        if (IDENTITY_PARAMETERS.count(parameter.get_name()) > 0) {
            result.successful = false;
            result.reason = parameter.get_name() + " can only be set at launch";
            return result;
        }
    }
    for (const rclcpp::Parameter& parameter : parameters) {
        const std::string applied = applyParameter(parameter, parameters);
        if (applied != "done") {
            result.successful = false;
            result.reason = parameter.get_name() + ": " + applied;
            RCLCPP_WARN_STREAM(LOGGER, "Rejected parameter change: " << result.reason);
            return result;
        }
        rememberApplied(parameter.get_name());
    }
    return result;
}

std::string BaslerDriverNode::applyParameter(const rclcpp::Parameter& parameter,
                                             const std::vector<rclcpp::Parameter>& batch) {
    const std::string& name = parameter.get_name();
    auto& parameters = parameters_;
    const auto pending_bool = [&](const std::string& n) { return pendingValue(n, batch).as_bool(); };
    const auto pending_double = [&](const std::string& n) { return pendingValue(n, batch).as_double(); };
    const auto pending_int = [&](const std::string& n) { return pendingValue(n, batch).as_int(); };

    if (name == "exposure") {
        float reached = 0.0f;
        parameters.exposure_ = parameter.as_double();
        if (!setExposure(static_cast<float>(parameter.as_double()), reached)) {
            return "Failed to set exposure to " + std::to_string(parameter.as_double());
        }
        RCLCPP_INFO_STREAM(
            LOGGER, "Exposure updated via parameter: requested=" << parameter.as_double() << " reached=" << reached);
        return "done";
    }
    if (name == "gain") {
        float reached = 0.0f;
        parameters.gain_ = parameter.as_double();
        if (!setGain(static_cast<float>(parameter.as_double()), reached)) {
            return "Failed to set gain to " + std::to_string(parameter.as_double());
        }
        RCLCPP_INFO_STREAM(LOGGER,
                           "Gain updated via parameter: requested=" << parameter.as_double() << " reached=" << reached);
        return "done";
    }
    if (name == "gamma") {
        float reached = 0.0f;
        parameters.gamma_ = parameter.as_double();
        return setGamma(static_cast<float>(parameter.as_double()), reached)
                   ? "done"
                   : "Failed to set gamma to " + std::to_string(parameter.as_double());
    }
    if (name == "brightness") {
        if (parameter.as_int() < 0 || parameter.as_int() > 255) {
            return "brightness must be within 0..255";
        }
        parameters.brightness_ = static_cast<int>(parameter.as_int());
        const bool exposure_auto = pending_bool("exposure_auto");
        const bool gain_auto = pending_bool("gain_auto");
        int reached = 0;
        const bool reached_target = setBrightness(parameters.brightness_, reached, exposure_auto, gain_auto);
        applyAutoFunctions(pending_bool("brightness_continuous"), exposure_auto, gain_auto);
        return reached_target ? "done"
                              : "Brightness " + std::to_string(parameters.brightness_) + " not reached (stuck at " +
                                    std::to_string(reached) + ")";
    }
    if (name == "brightness_continuous" || name == "exposure_auto" || name == "gain_auto") {
        parameters.brightness_continuous_ = pending_bool("brightness_continuous");
        parameters.exposure_auto_ = pending_bool("exposure_auto");
        parameters.gain_auto_ = pending_bool("gain_auto");
        applyAutoFunctions(parameters.brightness_continuous_, parameters.exposure_auto_, parameters.gain_auto_);
        return "done";
    }
    if (name == "binning_x" || name == "binning_y") {
        if (parameter.as_int() < 1 || parameter.as_int() > 32) {
            return name + " must be within 1..32";
        }
        std::size_t reached = 0;
        const auto target = static_cast<std::size_t>(parameter.as_int());
        const bool ok = name == "binning_x" ? setBinningX(target, reached) : setBinningY(target, reached);
        (name == "binning_x" ? parameters.binning_x_ : parameters.binning_y_) = target;
        return ok ? "done" : "Failed to set " + name;
    }
    if (name.rfind("roi.", 0) == 0) {
        sensor_msgs::msg::RegionOfInterest target;
        target.x_offset = static_cast<uint32_t>(std::max<int64_t>(0, pending_int("roi.x_offset")));
        target.y_offset = static_cast<uint32_t>(std::max<int64_t>(0, pending_int("roi.y_offset")));
        target.width = static_cast<uint32_t>(std::max<int64_t>(0, pending_int("roi.width")));
        target.height = static_cast<uint32_t>(std::max<int64_t>(0, pending_int("roi.height")));
        sensor_msgs::msg::RegionOfInterest reached;
        return setROI(target, reached) ? "done" : "Failed to set the region of interest";
    }
    if (name == "image_encoding") {
        const std::string result = camera_->setImageEncoding(parameter.as_string());
        const std::string restarted = restartGrabbing();
        if (result != "done") {
            return result;
        }
        return restarted;
    }
    if (name == "frame_rate") {
        const double frame_rate = parameter.as_double();
        if (frame_rate <= 0.0 && frame_rate != -1.0) {
            return "frame_rate must be positive (or -1 for the maximum)";
        }
        const double max_frame_rate = camera_->maxPossibleFramerate();
        if (frame_rate == -1.0 || frame_rate > max_frame_rate) {
            RCLCPP_WARN(LOGGER, "Desired framerate %.2f is higher than max possible. Grabbing at %.2f Hz", frame_rate,
                        max_frame_rate);
        }
        // A parameter can't be rewritten from its own callback, so frame_rate keeps the request.
        resetFrameTimer(frame_rate == -1.0 ? max_frame_rate : std::min(frame_rate, max_frame_rate));
        return "done";
    }
    if (name == "grab_timeout" || name == "trigger_timeout") {
        if (parameter.as_int() <= 0) {
            return name + " must be positive";
        }
        (name == "grab_timeout" ? parameters.grab_timeout_ : parameters.trigger_timeout_) =
            static_cast<int>(parameter.as_int());
        camera_->setTimeouts(parameters.grab_timeout_, parameters.trigger_timeout_);
        return "done";
    }
    if (name == "grab_strategy" || name == "max_num_buffer" || name == "output_queue_size" ||
        name == "max_transfer_size") {
        const int value = static_cast<int>(parameter.as_int());
        std::string result;
        if (name == "grab_strategy") {
            result = camera_->setGrabbingStrategy(value);
            parameters.grab_strategy_ = value;
        } else if (name == "max_num_buffer") {
            result = camera_->setMaxNumBuffer(value);
        } else if (name == "output_queue_size") {
            result = camera_->setOutputQueueSize(value);
        } else {
            result = camera_->setMaxTransferSize(value);
        }
        return result == "done" ? restartGrabbing() : result;
    }
    if (name == "white_balance_auto") {
        parameters.white_balance_auto_ = static_cast<int>(parameter.as_int());
        return camera_->setBalanceWhiteAuto(parameters.white_balance_auto_);
    }
    if (name.rfind("white_balance_ratio_", 0) == 0) {
        return camera_->setWhiteBalance(pending_double("white_balance_ratio_red"),
                                        pending_double("white_balance_ratio_green"),
                                        pending_double("white_balance_ratio_blue"));
    }
    if (name == "shutter_mode") {
        static const std::map<std::string, SHUTTER_MODE> modes = { { "rolling", SM_ROLLING },
                                                                   { "global", SM_GLOBAL },
                                                                   { "global_reset", SM_GLOBAL_RESET_RELEASE },
                                                                   { "", SM_DEFAULT } };
        auto it = modes.find(parameter.as_string());
        if (it == modes.end()) {
            return "shutter_mode must be rolling, global or global_reset";
        }
        parameters.shutter_mode_ = it->second;
        camera_->grabbingStopping();
        const std::string result = camera_->setShutterMode(it->second);
        const std::string restarted = restartGrabbing();
        return result == "done" ? restarted : result;
    }
    if (name == "auto_exposure_upper_limit") {
        parameters.auto_exposure_upper_limit_ = parameter.as_double();
        return camera_->setAutoExposureUpperLimit(parameter.as_double());
    }
    if (name == "enable_status_publisher") {
        parameters.enable_status_publisher_ = parameter.as_bool();
        return "done";
    }
    if (name == "downsampling_factor_exposure_search") {
        if (parameter.as_int() < 1) {
            return "downsampling_factor_exposure_search must be at least 1";
        }
        parameters.downsampling_factor_exposure_search_ = static_cast<int>(parameter.as_int());
        updateImageGeometry();
        return "done";
    }
    if (name == "exposure_search_timeout") {
        parameters.exposure_search_timeout_ = parameter.as_double();
        return "done";
    }
    if (name == "sleeping") {
        is_sleeping_ = parameter.as_bool();
        RCLCPP_INFO(LOGGER, is_sleeping_ ? "Sleeping: not publishing images" : "Waking up: publishing images again");
        return "done";
    }
    if (name == "trigger_selector" || name == "user_set_selector") {
        const auto entries =
            camera_->featureEntries(name == "trigger_selector" ? "TriggerSelector" : "UserSetSelector");
        if (!entries.empty() && std::find(entries.begin(), entries.end(), parameter.as_string()) == entries.end()) {
            return "must be one of: " + joined(entries);
        }
        return "done";
    }
    if (name == "pfs_file") {
        return loadPfs(parameter.as_string());
    }
    for (const FeatureParameter& feature : feature_parameters_) {
        if (feature.name != name) {
            continue;
        }
        if (parameter.get_type() == rclcpp::ParameterType::PARAMETER_STRING && parameter.as_string().empty()) {
            return "done";  // the fallback of a feature that wasn't readable: nothing to set
        }
        FeatureSelection selection = selectionOf(feature);
        if (feature.trigger_selected) {
            selection.value = pendingValue("trigger_selector", batch).as_string();
        }
        return camera_->setFeature(feature.features.front(), parameter.get_parameter_value(), selection);
    }
    // Parameters without a camera effect (downsampling, timeouts already handled, ...).
    return "done";
}

void BaslerDriverNode::applyAutoFunctions(bool brightness_continuous, bool exposure_auto, bool gain_auto) {
    if (!brightness_continuous) {
        camera_->disableAllRunningAutoBrightessFunctions();
        return;
    }
    camera_->disableAllRunningAutoBrightessFunctions();
    if (exposure_auto) {
        camera_->enableContinuousAutoExposure();
    }
    if (gain_auto) {
        camera_->enableContinuousAutoGain();
    }
}

bool BaslerDriverNode::setExposure(const float& target_exposure, float& reached_exposure) {
    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    if (camera_->setExposure(target_exposure, reached_exposure)) {
        return true;
    }
    // Retry until the camera accepts it (it may be busy), as the original does.
    rclcpp::Rate r(10.0);
    const rclcpp::Time timeout = now() + rclcpp::Duration::from_seconds(5);
    while (rclcpp::ok() && now() < timeout) {
        if (camera_->setExposure(target_exposure, reached_exposure)) {
            return true;
        }
        r.sleep();
    }
    RCLCPP_ERROR(LOGGER, "Error in setExposure(): Unable to set target exposure before timeout");
    return false;
}

bool BaslerDriverNode::setGain(const float& target_gain, float& reached_gain) {
    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    if (camera_->setGain(target_gain, reached_gain)) {
        return true;
    }
    rclcpp::Rate r(10.0);
    const rclcpp::Time timeout = now() + rclcpp::Duration::from_seconds(5);
    while (rclcpp::ok() && now() < timeout) {
        if (camera_->setGain(target_gain, reached_gain)) {
            return true;
        }
        r.sleep();
    }
    RCLCPP_ERROR(LOGGER, "Error in setGain(): Unable to set target gain before timeout");
    return false;
}

bool BaslerDriverNode::setGamma(const float& target_gamma, float& reached_gamma) {
    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    return camera_->setGamma(target_gamma, reached_gamma);
}

bool BaslerDriverNode::setROI(const sensor_msgs::msg::RegionOfInterest& target_roi,
                              sensor_msgs::msg::RegionOfInterest& reached_roi) {
    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    const bool ok = camera_->setROI(target_roi, reached_roi);
    sensor_msgs::msg::CameraInfo cam_info = camera_info_manager_->getCameraInfo();
    cam_info.roi = camera_->currentROI();
    camera_info_manager_->setCameraInfo(cam_info);
    updateImageGeometry();
    return ok;
}

bool BaslerDriverNode::setBinningX(const std::size_t& target_binning_x, std::size_t& reached_binning_x) {
    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    const bool ok = camera_->setBinningX(target_binning_x, reached_binning_x);
    sensor_msgs::msg::CameraInfo cam_info = camera_info_manager_->getCameraInfo();
    cam_info.binning_x = static_cast<uint32_t>(camera_->currentBinningX());
    camera_info_manager_->setCameraInfo(cam_info);
    updateImageGeometry();
    return ok;
}

bool BaslerDriverNode::setBinningY(const std::size_t& target_binning_y, std::size_t& reached_binning_y) {
    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    const bool ok = camera_->setBinningY(target_binning_y, reached_binning_y);
    sensor_msgs::msg::CameraInfo cam_info = camera_info_manager_->getCameraInfo();
    cam_info.binning_y = static_cast<uint32_t>(camera_->currentBinningY());
    camera_info_manager_->setCameraInfo(cam_info);
    updateImageGeometry();
    return ok;
}

bool BaslerDriverNode::setBrightness(const int& target_brightness, int& reached_brightness, const bool& exposure_auto,
                                     const bool& gain_auto) {
    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    const rclcpp::Time begin = now();
    const int target = std::min(255, target_brightness);

    // Start from the exposure that reached this brightness before, if any.
    if (brightness_exp_lut_.at(target) != 0.0f) {
        float reached_exp = 0.0f;
        if (!setExposure(brightness_exp_lut_.at(target), reached_exp)) {
            RCLCPP_WARN(LOGGER,
                        "Tried to speed-up exposure search with initial guess, but setting the exposure failed!");
        }
    }
    if (!grabImage()) {
        RCLCPP_ERROR(LOGGER, "Failed to grab image, can't calculate current brightness!");
        return false;
    }
    float current_brightness = calcCurrentBrightness();
    if (std::abs(current_brightness - static_cast<float>(target)) <= 1.0f) {
        reached_brightness = static_cast<int>(current_brightness);
        return true;
    }

    camera_->disableAllRunningAutoBrightessFunctions();
    if (target <= 50 && brightness_exp_lut_.at(50) != 0.0f) {
        float reached_exp = 0.0f;
        setExposure(brightness_exp_lut_.at(50), reached_exp);
    }
    if (!exposure_auto && !gain_auto) {
        RCLCPP_WARN_STREAM(LOGGER,
                           "Neither Auto Exposure Time ('exposure_auto') nor Auto Gain ('gain_auto') are "
                           "enabled! Hence gain and exposure time are assumed to be fix and the target "
                           "brightness ("
                               << target << ") can not be reached!");
        return false;
    }

    bool is_brightness_reached = false;
    std::size_t fail_safe_ctr = 0;
    const std::size_t fail_safe_ctr_limit = 10;
    float last_brightness = std::numeric_limits<float>::max();
    const rclcpp::Time start_time = now();
    const double timeout_s = parameters_.exposure_search_timeout_ + (target < 205 ? 0.0 : 10.0);
    const rclcpp::Time timeout = start_time + rclcpp::Duration::from_seconds(timeout_s);

    while (rclcpp::ok()) {
        if (!camera_->setBrightness(target, current_brightness, exposure_auto, gain_auto)) {
            camera_->disableAllRunningAutoBrightessFunctions();
            break;
        }
        if (!grabImage()) {
            return false;
        }
        if (camera_->isAutoBrightnessFunctionRunning()) {
            // "Once" auto functions switch themselves off when done; keep grabbing until then.
            if (now() > timeout) {
                camera_->disableAllRunningAutoBrightessFunctions();
                reached_brightness = static_cast<int>(calcCurrentBrightness());
                RCLCPP_WARN(LOGGER, "Did not reach the target brightness before timeout");
                return false;
            }
            continue;
        }
        current_brightness = calcCurrentBrightness();
        is_brightness_reached =
            std::abs(current_brightness - static_cast<float>(target)) < camera_->maxBrightnessTolerance();
        if (is_brightness_reached) {
            camera_->disableAllRunningAutoBrightessFunctions();
            break;
        }
        fail_safe_ctr = std::abs(last_brightness - current_brightness) <= 1.0f ? fail_safe_ctr + 1 : 0;
        last_brightness = current_brightness;
        if (fail_safe_ctr > fail_safe_ctr_limit) {
            RCLCPP_WARN_STREAM(LOGGER, "Seems like the desired brightness ("
                                           << target << ") is not reachable! Stuck at brightness " << current_brightness
                                           << " and exposure " << camera_->currentExposure() << "us");
            camera_->disableAllRunningAutoBrightessFunctions();
            reached_brightness = static_cast<int>(current_brightness);
            return false;
        }
        if (now() > timeout) {
            camera_->disableAllRunningAutoBrightessFunctions();
            RCLCPP_WARN_STREAM(LOGGER, "Did not reach the target brightness before timeout of "
                                           << timeout_s << " sec! Stuck at brightness " << current_brightness
                                           << " and exposure " << camera_->currentExposure() << "us");
            reached_brightness = static_cast<int>(current_brightness);
            return false;
        }
    }

    reached_brightness = static_cast<int>(current_brightness);
    if (is_brightness_reached) {
        for (const int index : { std::clamp(reached_brightness, 0, 255), target }) {
            float& entry = brightness_exp_lut_.at(index);
            entry = entry == 0.0f ? camera_->currentExposure() : 0.5f * (entry + camera_->currentExposure());
        }
    }
    RCLCPP_DEBUG_STREAM(LOGGER, "Brightness search took " << (now() - begin).seconds() << " s");
    return is_brightness_reached;
}

void BaslerDriverNode::setupSamplingIndices(std::vector<std::size_t>& indices, std::size_t rows, std::size_t cols,
                                            int downsampling_factor) {
    indices.clear();
    const std::size_t min_window_height =
        static_cast<std::size_t>(static_cast<float>(rows) / static_cast<float>(std::max(1, downsampling_factor)));
    indices.push_back(static_cast<std::size_t>(0.5 * rows * cols));
    genSamplingIndicesRec(indices, min_window_height, cv::Point2i(0, 0),
                          cv::Point2i(static_cast<int>(cols), static_cast<int>(rows)));
    std::sort(indices.begin(), indices.end());
}

void BaslerDriverNode::genSamplingIndicesRec(std::vector<std::size_t>& indices, const std::size_t& min_window_height,
                                             const cv::Point2i& s, const cv::Point2i& e) {
    if (static_cast<std::size_t>(std::abs(e.y - s.y)) <= min_window_height) {
        return;
    }
    // Sample the centres of the four sub-windows' borders, then recurse into each quadrant:
    //   s . . . . . .      a: centre, b/c/d/f: below/left/above/right of it
    //   . . . d . . .
    //   . c . a . f .
    //   . . . b . . .
    //   . . . . . . e
    const cv::Point2i a = s + 0.5 * (e - s);
    const cv::Point2i delta = 0.5 * (e - s);
    const auto cols = static_cast<int>(camera_->imageCols());
    for (const cv::Point2i& p : { s + cv::Point2i(delta.x, static_cast<int>(1.5 * delta.y)),
                                  s + cv::Point2i(static_cast<int>(0.5 * delta.x), delta.y),
                                  s + cv::Point2i(delta.x, static_cast<int>(0.5 * delta.y)),
                                  s + cv::Point2i(static_cast<int>(1.5 * delta.x), delta.y) }) {
        indices.push_back(static_cast<std::size_t>(p.y * cols + p.x));
    }
    genSamplingIndicesRec(indices, min_window_height, s, a);
    genSamplingIndicesRec(indices, min_window_height, a, e);
    genSamplingIndicesRec(indices, min_window_height, cv::Point2i(s.x, a.y), cv::Point2i(a.x, e.y));
    genSamplingIndicesRec(indices, min_window_height, cv::Point2i(a.x, s.y), cv::Point2i(e.x, a.y));
}

float BaslerDriverNode::calcCurrentBrightness() {
    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    const std::vector<uint8_t>& data = img_raw_msg_.data;
    if (data.empty()) {
        return 0.0f;
    }
    if (sensor_msgs::image_encodings::isMono(img_raw_msg_.encoding)) {
        if (data.size() != static_cast<std::size_t>(img_raw_msg_.height) * img_raw_msg_.width) {
            return 0.0f;
        }
        float sum = 0.0f;
        for (const std::size_t index : sampling_indices_) {
            sum += data.at(index);
        }
        return sampling_indices_.empty() ? 0.0f : sum / static_cast<float>(sampling_indices_.size());
    }
    const double sum = std::accumulate(data.begin(), data.end(), 0.0);
    return static_cast<float>(sum / static_cast<double>(data.size()));
}

void BaslerDriverNode::setupInitialCameraInfo(sensor_msgs::msg::CameraInfo& cam_info_msg) {
    camera_->getInitialCameraInfo(cam_info_msg);
    cam_info_msg.header.frame_id = parameters_.cameraFrame();
    cam_info_msg.header.stamp = now();
}

void BaslerDriverNode::setupRectification() {
    if (!img_rect_pub_) {
        img_rect_pub_ = std::make_unique<image_transport::Publisher>(
            image_transport::create_publisher(*this, "~/image_rect", rclcpp::QoS(10)));
    }
    if (!pinhole_model_) {
        pinhole_model_ = std::make_unique<image_geometry::PinholeCameraModel>();
    }
    pinhole_model_->fromCameraInfo(camera_info_manager_->getCameraInfo());
    if (!cv_bridge_img_rect_) {
        cv_bridge_img_rect_ = std::make_unique<cv_bridge::CvImage>();
    }
    cv_bridge_img_rect_->header = img_raw_msg_.header;
    cv_bridge_img_rect_->encoding = img_raw_msg_.encoding;
}

uint32_t BaslerDriverNode::getNumSubscribersRectImagePub() const {
    return camera_info_manager_->isCalibrated() && img_rect_pub_
               ? static_cast<uint32_t>(img_rect_pub_->getNumSubscribers())
               : 0;
}

void BaslerDriverNode::createDiagnostics(diagnostic_updater::DiagnosticStatusWrapper& stat) {
    std::lock_guard<std::recursive_mutex> lock(grab_mutex_);
    if (camera_) {
        diagnostics_updater_.setHardwareID(camera_->deviceUserID());
        stat.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "Device is connected");
        stat.add("temperature", camera_->getTemperature());
        stat.add("total_buffer_count", camera_->getStatisticTotalBufferCount());
        stat.add("failed_buffer_count", camera_->getStatisticFailedBufferCount());
        stat.add("buffer_underrun_count", camera_->getStatisticBufferUnderrunCount());
        stat.add("missed_frame_count", camera_->getStatisticMissedFrameCount());
    } else {
        diagnostics_updater_.setHardwareID(parameters_.deviceUserID());
        stat.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR, "No camera connected");
        publishStatus(ComponentStatus::ERROR, "No camera connected");
    }
}

void BaslerDriverNode::createCameraInfoDiagnostics(diagnostic_updater::DiagnosticStatusWrapper& stat) {
    if (camera_info_manager_->isCalibrated()) {
        stat.summary(diagnostic_msgs::msg::DiagnosticStatus::OK, "Intrinsic calibration found");
    } else {
        stat.summary(diagnostic_msgs::msg::DiagnosticStatus::ERROR, "No intrinsic calibration found");
    }
}

}  // namespace basler_driver

RCLCPP_COMPONENTS_REGISTER_NODE(basler_driver::BaslerDriverNode)
