#include "basler_driver/basler_camera.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <map>
#include <rclcpp/logging.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <sstream>
#include <thread>

#include "basler_driver/encoding_conversions.hpp"

namespace basler_driver {

namespace {

const rclcpp::Logger LOGGER = rclcpp::get_logger("basler_driver.camera");

const char* const NOT_SUPPORTED = "The connected Camera not supporting this feature";

// Entries of BalanceWhiteAuto, by the white_balance_auto code.
const std::vector<std::string> BALANCE_WHITE_AUTO = { "Off", "Once", "Continuous" };

}  // namespace

std::unique_ptr<BaslerCamera> BaslerCamera::create(OpenByType open_by_type, const std::string& id) {
    arv_update_device_list();
    const unsigned int n_devices = arv_get_n_devices();
    for (unsigned int i = 0; i < n_devices; ++i) {
        const char* protocol = arv_get_device_protocol(i);
        const char* vendor = arv_get_device_vendor(i);
        const char* serial = arv_get_device_serial_nbr(i);
        // USB3 Vision only. "Fake" is Aravis' simulated camera, listed only once a test enables it.
        if (protocol == nullptr || (std::strcmp(protocol, "USB3Vision") != 0 && std::strcmp(protocol, "Fake") != 0)) {
            continue;
        }
        if (open_by_type == OpenByType::FirstFound &&
            (vendor == nullptr || std::string(vendor).find("Basler") == std::string::npos)) {
            continue;
        }
        if (open_by_type == OpenByType::DeviceSerial && (serial == nullptr || id != serial)) {
            continue;
        }

        GError* error = nullptr;
        ArvCamera* camera = arv_camera_new(arv_get_device_id(i), &error);
        if (camera == nullptr) {
            RCLCPP_WARN_STREAM(LOGGER, "Unable to open camera " << arv_get_device_id(i) << ": "
                                                                << (error != nullptr ? error->message : "unknown"));
            g_clear_error(&error);
            continue;
        }
        std::unique_ptr<BaslerCamera> result(new BaslerCamera(camera));
        // The user ID lives in the camera's own memory, so the device has to be open to compare it.
        if (open_by_type == OpenByType::DeviceUserID && result->device_user_id_ != id) {
            continue;
        }
        RCLCPP_INFO_STREAM(LOGGER, "Opened " << (vendor != nullptr ? vendor : "") << " " << arv_get_device_model(i)
                                             << " (serial " << (serial != nullptr ? serial : "") << ", user ID '"
                                             << result->device_user_id_ << "')");
        return result;
    }
    return nullptr;
}

BaslerCamera::BaslerCamera(ArvCamera* camera) : camera_(camera), genicam_(arv_camera_get_device(camera)) {
    // Out-of-range writes fail the way GenApi's OutOfRangeException does rather than being clamped.
    arv_camera_set_range_check_policy(camera_, ARV_RANGE_CHECK_POLICY_ENABLE);
    g_signal_connect(arv_camera_get_device(camera_), "control-lost", G_CALLBACK(&BaslerCamera::onControlLost), this);
    chunk_parser_ = arv_camera_create_chunk_parser(camera_);
    try {
        if (genicam_.isReadable("DeviceUserID")) {
            device_user_id_ = genicam_.getString("DeviceUserID");
        }
        updateImageSize();
    } catch (const GenicamException& e) {
        RCLCPP_WARN_STREAM(LOGGER, "Error while reading the camera's identity: " << e.what());
    }
}

BaslerCamera::~BaslerCamera() {
    grabbingStopping();
    g_signal_handlers_disconnect_by_data(arv_camera_get_device(camera_), this);
    g_clear_object(&chunk_parser_);
    g_object_unref(camera_);
}

void BaslerCamera::onControlLost(ArvDevice* device, gpointer user_data) {
    (void)device;
    static_cast<BaslerCamera*>(user_data)->removed_ = true;
}

std::string BaslerCamera::firstAvailable(std::initializer_list<const char*> names) const {
    for (const char* name : names) {
        if (genicam_.isAvailable(name)) {
            return name;
        }
    }
    return "";
}

std::string BaslerCamera::exposureFeature() const { return firstAvailable({ "ExposureTime", "ExposureTimeAbs" }); }

std::string BaslerCamera::gainFeature() const { return firstAvailable({ "Gain", "GainRaw" }); }

void BaslerCamera::select(const FeatureSelection& selection) const {
    if (!selection.selector.empty() && genicam_.isAvailable(selection.selector)) {
        genicam_.setEnum(selection.selector, selection.value);
    }
}

bool BaslerCamera::applySoftwareTriggerConfiguration() {
    try {
        grabbingStopping();
        // Disable every trigger but the one that starts frames, and fire that one from software.
        if (genicam_.isAvailable("TriggerSelector")) {
            const std::vector<std::string> selectors = genicam_.enumEntries("TriggerSelector");
            const bool has_frame_start = std::find(selectors.begin(), selectors.end(), "FrameStart") != selectors.end();
            const std::string frame_trigger = has_frame_start ? "FrameStart" : "AcquisitionStart";
            for (const std::string& selector : selectors) {
                genicam_.setEnum("TriggerSelector", selector);
                if (genicam_.isWritable("TriggerMode")) {
                    genicam_.setEnum("TriggerMode", "Off");
                }
            }
            genicam_.setEnum("TriggerSelector", frame_trigger);
            genicam_.setEnum("TriggerMode", "On");
            genicam_.setEnum("TriggerSource", "Software");
        }
        if (genicam_.isWritable("AcquisitionMode")) {
            genicam_.setEnum("AcquisitionMode", "Continuous");
        }
    } catch (const GenicamException& e) {
        RCLCPP_ERROR_STREAM(LOGGER, "Error while registering the software trigger configuration: " << e.what());
        return false;
    }
    return true;
}

bool BaslerCamera::applyCamSpecificStartupSettings(const BaslerDriverParameters& parameters) {
    try {
        grabbingStopping();
        const std::string& user_set = parameters.startup_user_set_;
        RCLCPP_INFO_STREAM(LOGGER, "Startup user profile set to \"" << user_set << "\"");
        if (user_set == "Default" || user_set == "UserSet1" || user_set == "UserSet2" || user_set == "UserSet3") {
            genicam_.setEnum("UserSetSelector", user_set);
            genicam_.execute("UserSetLoad");
            RCLCPP_INFO_STREAM(LOGGER, user_set << " loaded");
        } else if (!user_set.empty() && user_set != "CurrentSetting") {
            RCLCPP_WARN_STREAM(LOGGER, "Unsupported startup user profile \"" << user_set << "\", ignoring");
        }

        if (user_set == "Default") {
            // The default user set free-runs; put the software trigger back.
            genicam_.setEnum("TriggerSource", "Software");
            genicam_.setEnum("TriggerMode", "On");
            if (genicam_.isWritable("AutoGainLowerLimit") && genicam_.isWritable("AutoGainUpperLimit")) {
                const std::string gain = gainFeature();
                genicam_.setFloat("AutoGainLowerLimit", genicam_.getFloatMin(gain));
                genicam_.setFloat("AutoGainUpperLimit", genicam_.getFloatMax(gain));
            }
        }
        if (user_set == "Default" || user_set == "CurrentSetting" || user_set.empty()) {
            // Lower limit: room to fight 50Hz flicker; upper limit: bounds motion blur.
            const std::string exposure = exposureFeature();
            if (!exposure.empty() && genicam_.isWritable("AutoExposureTimeLowerLimit") &&
                genicam_.isWritable("AutoExposureTimeUpperLimit")) {
                const double upper = std::min(parameters.auto_exposure_upper_limit_, genicam_.getFloatMax(exposure));
                genicam_.setFloat("AutoExposureTimeLowerLimit", genicam_.getFloatMin(exposure));
                genicam_.setFloat("AutoExposureTimeUpperLimit", upper);
                RCLCPP_INFO_STREAM(LOGGER, "Cam has auto exposure range: [" << genicam_.getFloatMin(exposure) << " - "
                                                                            << upper << "] us");
            }
        }

        if (genicam_.isAvailable("BinningHorizontal") && genicam_.isAvailable("BinningVertical")) {
            RCLCPP_INFO_STREAM(LOGGER, "Cam has binning range: x(hz) = ["
                                           << genicam_.getIntMin("BinningHorizontal") << " - "
                                           << genicam_.getIntMax("BinningHorizontal") << "], y(vt) = ["
                                           << genicam_.getIntMin("BinningVertical") << " - "
                                           << genicam_.getIntMax("BinningVertical") << "].");
        } else {
            RCLCPP_INFO(LOGGER, "Cam does not support binning.");
        }
        if (const std::string exposure = exposureFeature(); !exposure.empty()) {
            RCLCPP_INFO_STREAM(LOGGER, "Cam has exposure time range: [" << genicam_.getFloatMin(exposure) << " - "
                                                                        << genicam_.getFloatMax(exposure)
                                                                        << "] measured in microseconds.");
        }
        if (const std::string gain = gainFeature(); !gain.empty()) {
            RCLCPP_INFO_STREAM(LOGGER, "Cam has gain range: [" << genicam_.getFloatMin(gain) << " - "
                                                               << genicam_.getFloatMax(gain) << "].");
        }
        if (genicam_.isAvailable("Gamma")) {
            RCLCPP_INFO_STREAM(LOGGER, "Cam has gamma range: [" << genicam_.getFloatMin("Gamma") << " - "
                                                                << genicam_.getFloatMax("Gamma") << "].");
        }
        if (genicam_.isAvailable("AutoTargetBrightness")) {
            RCLCPP_INFO_STREAM(LOGGER, "Cam has auto brightness range: ["
                                           << genicam_.getFloatMin("AutoTargetBrightness") * 255 << " - "
                                           << genicam_.getFloatMax("AutoTargetBrightness") * 255
                                           << "] which is the average pixel intensity.");
        }
    } catch (const GenicamException& e) {
        RCLCPP_ERROR_STREAM(LOGGER, "Error applying camera specific startup setting for USB cameras: " << e.what());
        return false;
    }
    return true;
}

bool BaslerCamera::startGrabbing(const BaslerDriverParameters& parameters) {
    try {
        grabbingStopping();
        {
            // A previous process may have died mid-acquisition, leaving the camera streaming with its
            // transport-layer parameters (PixelFormat, size) locked. Stop it unconditionally.
            GError* stop_error = nullptr;
            arv_camera_stop_acquisition(camera_, &stop_error);
            g_clear_error(&stop_error);
        }
        if (genicam_.isAvailable("ShutterMode")) {
            const std::string result = setShutterMode(parameters.shutter_mode_);
            if (result != "done") {
                RCLCPP_WARN_STREAM(LOGGER, "Unable to set the shutter mode: " << result);
            }
        }

        available_image_encodings_ = detectAvailableImageEncodings(true);
        const std::string result = setImageEncoding(parameters.imageEncoding());
        if (result != "done") {
            RCLCPP_WARN_STREAM(LOGGER, result);
            bool fallback_found = false;
            for (const std::string& gen_api_encoding : available_image_encodings_) {
                std::string ros_encoding;
                if (!encodingconversions::genAPI2Ros(gen_api_encoding, ros_encoding)) {
                    continue;
                }
                const std::string fallback_result = setImageEncoding(ros_encoding);
                if (fallback_result == "done") {
                    RCLCPP_WARN_STREAM(LOGGER, "Falling back to encoding: " << ros_encoding);
                    fallback_found = true;
                    break;
                }
                RCLCPP_WARN_STREAM(LOGGER, "Fallback encoding '" << ros_encoding << "' failed: " << fallback_result);
            }
            if (!fallback_found) {
                RCLCPP_ERROR_STREAM(LOGGER, "No usable image encoding found");
                return false;
            }
        }

        grab_strategy_ = parameters.grab_strategy_;
        setTimeouts(parameters.grab_timeout_, parameters.trigger_timeout_);
        if (genicam_.isReadable("DeviceUserID")) {
            device_user_id_ = genicam_.getString("DeviceUserID");
        }
        updateImageSize();

        const std::string started = grabbingStarting();
        if (started != "done") {
            RCLCPP_ERROR_STREAM(LOGGER, "Unable to start grabbing: " << started);
            return false;
        }
        std::vector<uint8_t> image;
        rclcpp::Time stamp;
        if (!grab(image, stamp)) {
            RCLCPP_ERROR(LOGGER, "Camera not ready because the result of the initial grab is invalid");
            return false;
        }
        is_ready_ = true;
    } catch (const GenicamException& e) {
        RCLCPP_ERROR_STREAM(LOGGER, "Error while starting to grab: " << e.what());
        return false;
    }
    return true;
}

void BaslerCamera::updateImageSize() {
    img_cols_ = static_cast<std::size_t>(genicam_.getInt("Width"));
    img_rows_ = static_cast<std::size_t>(genicam_.getInt("Height"));
    pixel_format_ = genicam_.getEnum("PixelFormat");
    GError* error = nullptr;
    const ArvPixelFormat format = arv_camera_get_pixel_format(camera_, &error);
    throwIfError(error);
    pixel_depth_ = static_cast<int>(ARV_PIXEL_FORMAT_BIT_PER_PIXEL(format) / 8);
    img_size_byte_ = img_cols_ * img_rows_ * static_cast<std::size_t>(pixel_depth_);
}

std::string BaslerCamera::grabbingStarting() {
    if (is_grabbing_) {
        return "done";
    }
    try {
        GError* error = nullptr;
        if (max_transfer_size_ > 0 && arv_camera_is_uv_device(camera_)) {
            arv_camera_uv_set_maximum_transfer_size(camera_, max_transfer_size_);
        }
        stream_ = arv_camera_create_stream(camera_, nullptr, nullptr, nullptr, &error);
        throwIfError(error);
        const guint payload = arv_camera_get_payload(camera_, &error);
        throwIfError(error);
        for (int i = 0; i < max_num_buffer_; ++i) {
            arv_stream_push_buffer(stream_, arv_buffer_new_allocate(payload));
        }
        has_last_frame_id_ = false;
        arv_camera_start_acquisition(camera_, &error);
        throwIfError(error);
        is_grabbing_ = true;
    } catch (const GenicamException& e) {
        destroyStream();
        return e.what();
    }
    return "done";
}

std::string BaslerCamera::grabbingStopping() {
    if (!is_grabbing_) {
        destroyStream();
        return "done";
    }
    is_grabbing_ = false;
    GError* error = nullptr;
    arv_camera_stop_acquisition(camera_, &error);
    destroyStream();
    if (error != nullptr && !removed_) {
        std::string message = error->message;
        g_error_free(error);
        return message;
    }
    g_clear_error(&error);
    return "done";
}

void BaslerCamera::currentStreamStatistics(guint64& completed, guint64& failures, guint64& underruns) const {
    completed = failures = underruns = 0;
    if (stream_ != nullptr) {
        arv_stream_get_statistics(stream_, &completed, &failures, &underruns);
    }
}

void BaslerCamera::destroyStream() {
    // Buffers popped from the stream are ours; the rest go with it.
    for (ArvBuffer* buffer : pending_) {
        g_object_unref(buffer);
    }
    pending_.clear();
    if (stream_ != nullptr) {
        guint64 completed, failures, underruns;
        currentStreamStatistics(completed, failures, underruns);
        completed_buffers_ += completed;
        failed_buffers_ += failures;
        underruns_ += underruns;
        g_clear_object(&stream_);
    }
}

void BaslerCamera::recycle(ArvBuffer* buffer) {
    if (stream_ != nullptr) {
        arv_stream_push_buffer(stream_, buffer);
    } else {
        g_object_unref(buffer);
    }
}

ArvBuffer* BaslerCamera::retrieveBuffer(guint64 timeout_us) {
    while (ArvBuffer* buffer = arv_stream_try_pop_buffer(stream_)) {
        pending_.push_back(buffer);
    }
    if (pending_.empty()) {
        ArvBuffer* buffer = arv_stream_timeout_pop_buffer(stream_, timeout_us);
        if (buffer == nullptr) {
            return nullptr;
        }
        pending_.push_back(buffer);
    }
    // Pylon's grab strategies: OneByOne hands out the oldest frame, LatestImageOnly the newest, and
    // LatestImages the oldest of the newest output_queue_size frames.
    std::size_t keep = pending_.size();
    if (grab_strategy_ == 1) {
        keep = 1;
    } else if (grab_strategy_ == 2) {
        keep = static_cast<std::size_t>(std::max(1, output_queue_size_));
    }
    while (pending_.size() > keep) {
        recycle(pending_.front());
        pending_.pop_front();
    }
    ArvBuffer* buffer = pending_.front();
    pending_.pop_front();
    return buffer;
}

bool BaslerCamera::isSoftwareTriggered() const {
    return genicam_.isAvailable("TriggerMode") && genicam_.getEnum("TriggerMode") == "On" &&
           genicam_.getEnum("TriggerSource") == "Software";
}

bool BaslerCamera::waitForFrameTriggerReady() {
    try {
        if (!genicam_.isWritable("AcquisitionStatusSelector") || !genicam_.isAvailable("AcquisitionStatus")) {
            RCLCPP_WARN_ONCE(LOGGER,
                             "The connected camera device does not support waiting for frame trigger ready. "
                             "Software trigger will be immediately executed.");
            return true;
        }
        genicam_.setEnum("AcquisitionStatusSelector", "FrameTriggerWait");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(trigger_timeout_);
        while (!genicam_.getBool("AcquisitionStatus")) {
            if (std::chrono::steady_clock::now() > deadline) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::microseconds(200));
        }
    } catch (const GenicamException& e) {
        RCLCPP_WARN_STREAM_ONCE(LOGGER,
                                "Unable to wait for frame trigger ready (" << e.what() << "); triggering immediately.");
    }
    return true;
}

bool BaslerCamera::grab(std::vector<uint8_t>& image, rclcpp::Time& stamp) {
    if (!is_grabbing_ || stream_ == nullptr) {
        return false;
    }
    ArvBuffer* buffer = nullptr;
    bool software_triggered = false;
    try {
        software_triggered = isSoftwareTriggered();
        if (software_triggered) {
            if (grab_strategy_ == 1) {
                // Only the frame this trigger produces is wanted.
                while (ArvBuffer* stale = arv_stream_try_pop_buffer(stream_)) {
                    recycle(stale);
                }
                for (ArvBuffer* stale : pending_) {
                    recycle(stale);
                }
                pending_.clear();
            }
            if (!waitForFrameTriggerReady()) {
                RCLCPP_ERROR(LOGGER,
                             "Error WaitForFrameTriggerReady() timed out, impossible to ExecuteSoftwareTrigger()");
                return false;
            }
            genicam_.execute("TriggerSoftware");
        }
    } catch (const GenicamException& e) {
        if (removed_) {
            RCLCPP_ERROR(LOGGER, "Lost connection to the camera . . .");
        } else {
            RCLCPP_ERROR_STREAM(LOGGER, "An image grabbing exception occurred: " << e.what());
        }
        return false;
    }

    buffer = retrieveBuffer(static_cast<guint64>(grab_timeout_) * 1000);
    if (buffer == nullptr) {
        if (removed_) {
            RCLCPP_ERROR(LOGGER, "Lost connection to the camera . . .");
        } else if (!software_triggered) {
            RCLCPP_ERROR(LOGGER, "Waiting for Trigger signal");
        } else {
            RCLCPP_ERROR(LOGGER, "An image grabbing exception occurred: timed out waiting for a frame");
        }
        return false;
    }

    const ArvBufferStatus status = arv_buffer_get_status(buffer);
    if (status != ARV_BUFFER_STATUS_SUCCESS) {
        RCLCPP_ERROR_STREAM(LOGGER, "Error: grab failed with Aravis buffer status " << static_cast<int>(status));
        recycle(buffer);
        return false;
    }
    const guint64 frame_id = arv_buffer_get_frame_id(buffer);
    if (has_last_frame_id_ && frame_id > last_frame_id_ + 1) {
        missed_frames_ += frame_id - last_frame_id_ - 1;
    }
    last_frame_id_ = frame_id;
    has_last_frame_id_ = true;

    size_t size = 0;
    const auto* data = static_cast<const uint8_t*>(arv_buffer_get_image_data(buffer, &size));
    if (data == nullptr || size < img_size_byte_) {
        RCLCPP_ERROR_STREAM(LOGGER, "Error: frame holds " << size << " bytes, expected " << img_size_byte_);
        recycle(buffer);
        return false;
    }
    image.resize(img_size_byte_);
    if (encodingconversions::is_12_bit_gen_api_enc(pixel_format_)) {
        // 12-bit pixels arrive LSB-aligned in 16 bits; ROS' 16-bit encodings expect them MSB-aligned.
        const auto* source = reinterpret_cast<const uint16_t*>(data);
        auto* target = reinterpret_cast<uint16_t*>(image.data());
        for (std::size_t i = 0; i < img_size_byte_ / 2; ++i) {
            target[i] = static_cast<uint16_t>(source[i] << 4);
        }
    } else {
        std::memcpy(image.data(), data, img_size_byte_);
    }

    try {
        if (genicam_.isAvailable("ChunkModeActive") && genicam_.getBool("ChunkModeActive") &&
            genicam_.isWritable("ChunkSelector")) {
            genicam_.setEnum("ChunkSelector", "Timestamp");
            if (genicam_.getBool("ChunkEnable")) {
                GError* error = nullptr;
                const gint64 timestamp =
                    arv_chunk_parser_get_integer_value(chunk_parser_, buffer, "ChunkTimestamp", &error);
                if (error == nullptr) {
                    stamp = rclcpp::Time(static_cast<int64_t>(timestamp), stamp.get_clock_type());
                } else {
                    RCLCPP_WARN_STREAM(LOGGER, "Error while trying to get the chunk timestamp: " << error->message);
                    g_error_free(error);
                }
            }
        }
    } catch (const GenicamException& e) {
        RCLCPP_WARN_STREAM(LOGGER, "An exception while getting the chunk timestamp occurred: " << e.what());
    }
    recycle(buffer);
    is_ready_ = true;
    return true;
}

std::string BaslerCamera::setFeature(const std::string& feature, const rclcpp::ParameterValue& value,
                                     const FeatureSelection& selection) {
    try {
        select(selection);
        if (!genicam_.isAvailable(feature)) {
            return NOT_SUPPORTED;
        }
        switch (value.get_type()) {
            case rclcpp::ParameterType::PARAMETER_BOOL: {
                const bool on = value.get<bool>();
                if (genicam_.enumEntries(feature).empty()) {
                    genicam_.setBool(feature, on);
                } else {
                    genicam_.setEnum(feature, on ? "On" : "Off");
                }
                break;
            }
            case rclcpp::ParameterType::PARAMETER_INTEGER:
                genicam_.setInt(feature, value.get<int64_t>());
                break;
            case rclcpp::ParameterType::PARAMETER_DOUBLE:
                genicam_.setFloat(feature, value.get<double>());
                break;
            case rclcpp::ParameterType::PARAMETER_STRING:
                genicam_.setEnum(feature, value.get<std::string>());
                break;
            default:
                return "Error: unsupported parameter type";
        }
    } catch (const GenicamException& e) {
        return e.what();
    }
    return "done";
}

std::optional<rclcpp::ParameterValue> BaslerCamera::getFeature(const std::string& feature, rclcpp::ParameterType type,
                                                               const FeatureSelection& selection) {
    try {
        select(selection);
        if (!genicam_.isReadable(feature)) {
            return std::nullopt;
        }
        switch (type) {
            case rclcpp::ParameterType::PARAMETER_BOOL:
                if (genicam_.enumEntries(feature).empty()) {
                    return rclcpp::ParameterValue(genicam_.getBool(feature));
                }
                return rclcpp::ParameterValue(genicam_.getEnum(feature) == "On");
            case rclcpp::ParameterType::PARAMETER_INTEGER:
                return rclcpp::ParameterValue(genicam_.getInt(feature));
            case rclcpp::ParameterType::PARAMETER_DOUBLE:
                return rclcpp::ParameterValue(genicam_.getFloat(feature));
            case rclcpp::ParameterType::PARAMETER_STRING:
                return rclcpp::ParameterValue(genicam_.getEnum(feature));
            default:
                return std::nullopt;
        }
    } catch (const GenicamException&) {
        return std::nullopt;
    }
}

std::vector<std::string> BaslerCamera::featureEntries(const std::string& feature) const {
    try {
        if (genicam_.isAvailable(feature)) {
            return genicam_.enumEntries(feature);
        }
    } catch (const GenicamException&) {
    }
    return {};
}

std::string BaslerCamera::currentROSEncoding() const {
    std::string ros_encoding;
    if (!encodingconversions::genAPI2Ros(pixel_format_, ros_encoding)) {
        RCLCPP_ERROR_STREAM_ONCE(LOGGER, "No ROS equivalent to GenApi encoding " << pixel_format_);
        return "";
    }
    return ros_encoding;
}

std::vector<std::string> BaslerCamera::detectAvailableImageEncodings(const bool& show_message) {
    std::vector<std::string> encodings = featureEntries("PixelFormat");
    if (show_message) {
        std::ostringstream message;
        message << "Cam supports the following [GenAPI|ROS] image encodings:";
        for (const std::string& encoding : encodings) {
            std::string ros_encoding = "NO_ROS_EQUIVALENT";
            encodingconversions::genAPI2Ros(encoding, ros_encoding);
            message << " ['" << encoding << "'|'" << ros_encoding << "']";
        }
        RCLCPP_INFO_STREAM(LOGGER, message.str());
    }
    return encodings;
}

std::string BaslerCamera::setImageEncoding(const std::string& ros_encoding) {
    const auto available = [this](const std::string& encoding) {
        return std::find(available_image_encodings_.begin(), available_image_encodings_.end(), encoding) !=
               available_image_encodings_.end();
    };
    // 16-bit ROS encodings: the true 16-bit pixel format when the camera has it, else the 12-bit one.
    std::string gen_api_encoding;
    const bool is_16bits_available =
        encodingconversions::ros2GenAPI(ros_encoding, gen_api_encoding, true) && available(gen_api_encoding);
    const bool conversion_found = encodingconversions::ros2GenAPI(ros_encoding, gen_api_encoding, is_16bits_available);
    if (!ros_encoding.empty() && !(conversion_found && available(gen_api_encoding))) {
        return "Error: unsupported/unknown image format";
    }
    if (!conversion_found) {
        // No encoding requested: the first of Mono8/RGB8 the camera offers.
        const auto fallback = std::find_if(available_image_encodings_.begin(), available_image_encodings_.end(),
                                           [](const std::string& e) { return e == "Mono8" || e == "RGB8"; });
        if (fallback == available_image_encodings_.end()) {
            return "Error: Couldn't find a fallback solution!";
        }
        gen_api_encoding = *fallback;
    }
    if (!genicam_.isAvailable("PixelFormat")) {
        return "Error : Camera does not support variable image pixel";
    }
    try {
        grabbingStopping();
        genicam_.setEnum("PixelFormat", gen_api_encoding);
        updateImageSize();
    } catch (const GenicamException& e) {
        return e.what();
    }
    return "done";
}

float BaslerCamera::maxPossibleFramerate() const {
    try {
        for (const char* feature : { "ResultingFrameRate", "ResultingFrameRateAbs", "AcquisitionFrameRate" }) {
            if (genicam_.isReadable(feature)) {
                return static_cast<float>(genicam_.getFloat(feature));
            }
        }
    } catch (const GenicamException& e) {
        RCLCPP_WARN_STREAM(LOGGER, "Unable to read the resulting frame rate: " << e.what());
    }
    // Unknown: don't limit the requested rate.
    return 1000.0f;
}

void BaslerCamera::getInitialCameraInfo(sensor_msgs::msg::CameraInfo& cam_info_msg) const {
    cam_info_msg.height = static_cast<uint32_t>(img_rows_);
    cam_info_msg.width = static_cast<uint32_t>(img_cols_);
    cam_info_msg.distortion_model = "";
    cam_info_msg.d = std::vector<double>(5, 0.);
    cam_info_msg.k.fill(0.0);
    cam_info_msg.r.fill(0.0);
    cam_info_msg.p.fill(0.0);
    cam_info_msg.binning_x = static_cast<uint32_t>(currentBinningX());
    cam_info_msg.binning_y = static_cast<uint32_t>(currentBinningY());
    cam_info_msg.roi.x_offset = cam_info_msg.roi.y_offset = 0;
    cam_info_msg.roi.height = cam_info_msg.roi.width = 0;
}

double BaslerCamera::numberOr(const std::string& feature, double not_available, double error) const {
    if (feature.empty() || !genicam_.isReadable(feature)) {
        return not_available;
    }
    try {
        return genicam_.getFloat(feature);
    } catch (const GenicamException&) {
        return error;
    }
}

float BaslerCamera::currentExposure() const { return static_cast<float>(numberOr(exposureFeature(), 0.0, 0.0)); }

float BaslerCamera::currentGain() const {
    const std::string gain = gainFeature();
    if (gain.empty()) {
        return 0.0f;
    }
    try {
        const double min = genicam_.getFloatMin(gain);
        const double max = genicam_.getFloatMax(gain);
        return max > min ? static_cast<float>((genicam_.getFloat(gain) - min) / (max - min)) : 0.0f;
    } catch (const GenicamException&) {
        return 0.0f;
    }
}

float BaslerCamera::currentGamma() const { return static_cast<float>(numberOr("Gamma", -10000.0, -20000.0)); }

std::size_t BaslerCamera::currentBinningX() const {
    return static_cast<std::size_t>(numberOr("BinningHorizontal", 1.0, 1.0));
}

std::size_t BaslerCamera::currentBinningY() const {
    return static_cast<std::size_t>(numberOr("BinningVertical", 1.0, 1.0));
}

std::size_t BaslerCamera::currentOffsetX() const { return static_cast<std::size_t>(numberOr("OffsetX", 0.0, 0.0)); }

std::size_t BaslerCamera::currentOffsetY() const { return static_cast<std::size_t>(numberOr("OffsetY", 0.0, 0.0)); }

sensor_msgs::msg::RegionOfInterest BaslerCamera::currentROI() const {
    sensor_msgs::msg::RegionOfInterest roi;
    roi.x_offset = static_cast<uint32_t>(currentOffsetX());
    roi.y_offset = static_cast<uint32_t>(currentOffsetY());
    roi.width = static_cast<uint32_t>(numberOr("Width", 0.0, 0.0));
    roi.height = static_cast<uint32_t>(numberOr("Height", 0.0, 0.0));
    roi.do_rectify = false;
    return roi;
}

bool BaslerCamera::setExposure(const float& target_exposure, float& reached_exposure) {
    try {
        const std::string exposure = exposureFeature();
        if (exposure.empty()) {
            RCLCPP_ERROR(LOGGER, "Error while trying to set exposure: the camera has no exposure time feature");
            return false;
        }
        if (genicam_.isWritable("ExposureAuto")) {
            genicam_.setEnum("ExposureAuto", "Off");
        }
        const double min = genicam_.getFloatMin(exposure);
        const double exposure_to_set = std::clamp<double>(target_exposure, min, genicam_.getFloatMax(exposure));
        if (exposure_to_set != target_exposure) {
            RCLCPP_WARN_STREAM(
                LOGGER, "Desired exposure (" << target_exposure << ") unreachable! Setting to " << exposure_to_set);
        }
        genicam_.setFloat(exposure, exposure_to_set);
        reached_exposure = currentExposure();
        // The camera rounds to its line time; more than the smallest exposure off counts as a failure.
        if (std::fabs(reached_exposure - exposure_to_set) > min) {
            return false;
        }
    } catch (const GenicamException& e) {
        RCLCPP_ERROR_STREAM(
            LOGGER, "An exception while setting target exposure to " << target_exposure << " occurred: " << e.what());
        return false;
    }
    return true;
}

bool BaslerCamera::setGain(const float& target_gain, float& reached_gain) {
    try {
        const std::string gain = gainFeature();
        if (gain.empty()) {
            RCLCPP_ERROR(LOGGER, "Error while trying to set gain: the camera has no gain feature");
            return false;
        }
        if (genicam_.isWritable("GainAuto")) {
            genicam_.setEnum("GainAuto", "Off");
        }
        const double min = genicam_.getFloatMin(gain);
        const double max = genicam_.getFloatMax(gain);
        const float truncated_gain = std::clamp(target_gain, 0.0f, 1.0f);
        if (truncated_gain != target_gain) {
            RCLCPP_WARN_STREAM(LOGGER, "Desired gain (" << target_gain << ") in percent out of range [0.0 - 1.0]! "
                                                        << "Setting to " << truncated_gain);
        }
        genicam_.setFloat(gain, min + truncated_gain * (max - min));
        reached_gain = currentGain();
    } catch (const GenicamException& e) {
        RCLCPP_ERROR_STREAM(LOGGER,
                            "An exception while setting target gain to " << target_gain << " occurred: " << e.what());
        return false;
    }
    return true;
}

bool BaslerCamera::setGamma(const float& target_gamma, float& reached_gamma) {
    if (!genicam_.isAvailable("Gamma")) {
        RCLCPP_ERROR(LOGGER, "Error while trying to set gamma: cam.Gamma NodeMap is not available!");
        return false;
    }
    try {
        const double gamma_to_set =
            std::clamp<double>(target_gamma, genicam_.getFloatMin("Gamma"), genicam_.getFloatMax("Gamma"));
        if (gamma_to_set != target_gamma) {
            RCLCPP_WARN_STREAM(LOGGER, "Desired gamma unreachable! Setting to " << gamma_to_set);
        }
        genicam_.setFloat("Gamma", gamma_to_set);
        reached_gamma = currentGamma();
    } catch (const GenicamException& e) {
        RCLCPP_ERROR_STREAM(LOGGER,
                            "An exception while setting target gamma to " << target_gamma << " occurred: " << e.what());
        return false;
    }
    return true;
}

bool BaslerCamera::setROI(const sensor_msgs::msg::RegionOfInterest& target_roi,
                          sensor_msgs::msg::RegionOfInterest& reached_roi) {
    for (const char* feature : { "Width", "Height", "OffsetX", "OffsetY" }) {
        if (!genicam_.isAvailable(feature)) {
            reached_roi = currentROI();
            return true;
        }
    }
    const bool was_grabbing = is_grabbing_;
    try {
        grabbingStopping();
        // Offsets first, so the full sensor is available for the new size.
        genicam_.setInt("OffsetX", 0);
        genicam_.setInt("OffsetY", 0);
        const int64_t max_width =
            genicam_.isAvailable("WidthMax") ? genicam_.getInt("WidthMax") : genicam_.getIntMax("Width");
        const int64_t max_height =
            genicam_.isAvailable("HeightMax") ? genicam_.getInt("HeightMax") : genicam_.getIntMax("Height");
        genicam_.setInt("Width", max_width);
        genicam_.setInt("Height", max_height);
        // Offsets move in steps of at least 2 so Bayer patterns keep their phase.
        const int64_t width_inc = std::max<int64_t>(2, genicam_.getIntInc("Width"));
        const int64_t height_inc = std::max<int64_t>(2, genicam_.getIntInc("Height"));

        const auto fit = [](int64_t value, int64_t min, int64_t max, int64_t inc) {
            value = std::clamp(value == 0 ? max : value, min, max);
            return value - (value - min) % inc;
        };
        const int64_t width =
            fit(target_roi.width, genicam_.getIntMin("Width"), max_width, genicam_.getIntInc("Width"));
        const int64_t height =
            fit(target_roi.height, genicam_.getIntMin("Height"), max_height, genicam_.getIntInc("Height"));
        const auto fit_offset = [](int64_t offset, int64_t size, int64_t max, int64_t inc) {
            if (offset % inc != 0) {
                offset += inc - offset % inc;
            }
            if (offset + size > max) {
                offset = max - size;
                offset -= offset % inc;
            }
            return offset;
        };
        genicam_.setInt("Width", width);
        genicam_.setInt("Height", height);
        genicam_.setInt("OffsetX", fit_offset(target_roi.x_offset, width, max_width, width_inc));
        genicam_.setInt("OffsetY", fit_offset(target_roi.y_offset, height, max_height, height_inc));
        reached_roi = currentROI();
        updateImageSize();
    } catch (const GenicamException& e) {
        RCLCPP_ERROR_STREAM(LOGGER, "An exception while setting target roi occurred: " << e.what());
        reached_roi = currentROI();
        if (was_grabbing) {
            grabbingStarting();
        }
        return false;
    }
    if (was_grabbing) {
        grabbingStarting();
    }
    return true;
}

bool BaslerCamera::setBinningX(const std::size_t& target_binning_x, std::size_t& reached_binning_x) {
    if (!genicam_.isAvailable("BinningHorizontal")) {
        RCLCPP_WARN(LOGGER, "Camera does not support binning (X). Will keep the current settings.");
        reached_binning_x = currentBinningX();
        return true;
    }
    const bool was_grabbing = is_grabbing_;
    try {
        grabbingStopping();
        const int64_t binning =
            std::clamp<int64_t>(static_cast<int64_t>(target_binning_x), genicam_.getIntMin("BinningHorizontal"),
                                genicam_.getIntMax("BinningHorizontal"));
        genicam_.setInt("BinningHorizontal", binning);
        reached_binning_x = currentBinningX();
        updateImageSize();
    } catch (const GenicamException& e) {
        RCLCPP_ERROR_STREAM(LOGGER, "An exception while setting target horizontal binning_x factor to "
                                        << target_binning_x << " occurred: " << e.what());
        reached_binning_x = currentBinningX();
        if (was_grabbing) {
            grabbingStarting();
        }
        return false;
    }
    if (was_grabbing) {
        grabbingStarting();
    }
    return true;
}

bool BaslerCamera::setBinningY(const std::size_t& target_binning_y, std::size_t& reached_binning_y) {
    if (!genicam_.isAvailable("BinningVertical")) {
        RCLCPP_WARN(LOGGER, "Camera does not support binning (Y). Will keep the current settings.");
        reached_binning_y = currentBinningY();
        return true;
    }
    const bool was_grabbing = is_grabbing_;
    try {
        grabbingStopping();
        const int64_t binning =
            std::clamp<int64_t>(static_cast<int64_t>(target_binning_y), genicam_.getIntMin("BinningVertical"),
                                genicam_.getIntMax("BinningVertical"));
        genicam_.setInt("BinningVertical", binning);
        reached_binning_y = currentBinningY();
        updateImageSize();
    } catch (const GenicamException& e) {
        RCLCPP_ERROR_STREAM(LOGGER, "An exception while setting target vertical binning_y factor to "
                                        << target_binning_y << " occurred: " << e.what());
        reached_binning_y = currentBinningY();
        if (was_grabbing) {
            grabbingStarting();
        }
        return false;
    }
    if (was_grabbing) {
        grabbingStarting();
    }
    return true;
}

std::string BaslerCamera::setShutterMode(const SHUTTER_MODE& shutter_mode) {
    static const std::map<SHUTTER_MODE, std::string> modes = { { SM_ROLLING, "Rolling" },
                                                               { SM_GLOBAL, "Global" },
                                                               { SM_GLOBAL_RESET_RELEASE, "GlobalResetRelease" } };
    auto it = modes.find(shutter_mode);
    if (it == modes.end()) {
        return "done";  // default_shutter_mode: leave the camera's setting alone
    }
    if (!genicam_.isAvailable("ShutterMode")) {
        return NOT_SUPPORTED;
    }
    try {
        genicam_.setEnum("ShutterMode", it->second);
    } catch (const GenicamException& e) {
        return e.what();
    }
    return "done";
}

std::string BaslerCamera::setAutoExposureUpperLimit(const double& upper_limit) {
    const std::string exposure = exposureFeature();
    if (exposure.empty() || !genicam_.isAvailable("AutoExposureTimeUpperLimit")) {
        return NOT_SUPPORTED;
    }
    try {
        genicam_.setFloat("AutoExposureTimeUpperLimit", std::min(upper_limit, genicam_.getFloatMax(exposure)));
    } catch (const GenicamException& e) {
        return e.what();
    }
    return "done";
}

bool BaslerCamera::isAutoBrightnessFunctionRunning() const {
    try {
        return (genicam_.isAvailable("ExposureAuto") && genicam_.getEnum("ExposureAuto") != "Off") ||
               (genicam_.isAvailable("GainAuto") && genicam_.getEnum("GainAuto") != "Off");
    } catch (const GenicamException&) {
        return false;
    }
}

void BaslerCamera::disableAllRunningAutoBrightessFunctions() {
    binary_exp_search_.reset();
    try {
        if (genicam_.isWritable("ExposureAuto")) {
            genicam_.setEnum("ExposureAuto", "Off");
        }
        if (genicam_.isWritable("GainAuto")) {
            genicam_.setEnum("GainAuto", "Off");
        }
    } catch (const GenicamException& e) {
        RCLCPP_WARN_STREAM(LOGGER, "Unable to disable the auto functions: " << e.what());
    }
}

void BaslerCamera::enableContinuousAutoExposure() {
    try {
        if (genicam_.isWritable("ExposureAuto")) {
            genicam_.setEnum("ExposureAuto", "Continuous");
        } else {
            RCLCPP_WARN(LOGGER, "Trying to enable ExposureAuto_Continuous mode, but the camera has no Auto Exposure");
        }
    } catch (const GenicamException& e) {
        RCLCPP_WARN_STREAM(LOGGER, "Unable to enable continuous auto exposure: " << e.what());
    }
}

void BaslerCamera::enableContinuousAutoGain() {
    try {
        if (genicam_.isWritable("GainAuto")) {
            genicam_.setEnum("GainAuto", "Continuous");
        } else {
            RCLCPP_WARN(LOGGER, "Trying to enable GainAuto_Continuous mode, but the camera has no Auto Gain");
        }
    } catch (const GenicamException& e) {
        RCLCPP_WARN_STREAM(LOGGER, "Unable to enable continuous auto gain: " << e.what());
    }
}

bool BaslerCamera::setBrightness(const int& target_brightness, const float& current_brightness,
                                 const bool& exposure_auto, const bool& gain_auto) {
    try {
        const int target = std::min(255, target_brightness);
        if (isAutoBrightnessFunctionRunning()) {
            // Let a running "Once" auto function finish.
            return true;
        }
        if (!genicam_.isWritable("AutoTargetBrightness")) {
            return setExtendedBrightness(target, current_brightness);
        }
        const double min = genicam_.getFloatMin("AutoTargetBrightness");
        const double max = genicam_.getFloatMax("AutoTargetBrightness");
        const double brightness_to_set = target / 255.0;
        if (min <= brightness_to_set && brightness_to_set <= max) {
            genicam_.setFloat("AutoTargetBrightness", brightness_to_set);
        } else if (binary_exp_search_) {
            return setExtendedBrightness(target, current_brightness);
        } else {
            // Pre-control to the nearest reachable brightness with the auto functions, then bisect.
            genicam_.setFloat("AutoTargetBrightness", brightness_to_set < min ? min : max);
            const float exposure = currentExposure();
            const float lower = static_cast<float>(numberOr("AutoExposureTimeLowerLimit", exposure, exposure));
            const float upper = static_cast<float>(numberOr("AutoExposureTimeUpperLimit", exposure, exposure));
            binary_exp_search_ = std::make_unique<BinaryExposureSearch>(static_cast<float>(target),
                                                                        brightness_to_set < min ? lower : exposure,
                                                                        brightness_to_set < min ? exposure : upper);
        }
        if (exposure_auto && genicam_.isWritable("ExposureAuto")) {
            genicam_.setEnum("ExposureAuto", "Once");
        }
        if (gain_auto && genicam_.isWritable("GainAuto")) {
            genicam_.setEnum("GainAuto", "Once");
        }
    } catch (const GenicamException& e) {
        RCLCPP_ERROR_STREAM(LOGGER, "An exception while setting target brightness to " << target_brightness
                                                                                       << " occurred: " << e.what());
        return false;
    }
    return true;
}

bool BaslerCamera::setExtendedBrightness(const int& target_brightness, const float& current_brightness) {
    if (target_brightness <= 0 || target_brightness > 255) {
        return false;
    }
    if (!binary_exp_search_) {
        const float exposure = currentExposure();
        const std::string feature = exposureFeature();
        binary_exp_search_ = std::make_unique<BinaryExposureSearch>(
            static_cast<float>(target_brightness),
            static_cast<float>(numberOr(feature, 0.0, 0.0) > 0 ? genicam_.getFloatMin(feature) : exposure),
            static_cast<float>(genicam_.getFloatMax(feature)));
    }
    if (binary_exp_search_->isLimitReached() || !binary_exp_search_->update(current_brightness, currentExposure())) {
        disableAllRunningAutoBrightessFunctions();
        return false;
    }
    float reached_exposure = 0.0f;
    if (!setExposure(binary_exp_search_->newExposure(), reached_exposure)) {
        return false;
    }
    // At either end of the exposure range: one more frame decides, then the search gives up.
    const std::string feature = exposureFeature();
    if (reached_exposure <= genicam_.getFloatMin(feature) || reached_exposure >= genicam_.getFloatMax(feature)) {
        binary_exp_search_->limitReached(true);
    }
    return true;
}

std::string BaslerCamera::setWhiteBalance(const double& red, const double& green, const double& blue) {
    if (!genicam_.isAvailable("BalanceRatio") || !genicam_.isAvailable("BalanceRatioSelector")) {
        RCLCPP_ERROR(LOGGER,
                     "Error while trying to set the white balance. The connected Camera not supporting "
                     "this feature");
        return NOT_SUPPORTED;
    }
    try {
        if (genicam_.isWritable("BalanceWhiteAuto")) {
            genicam_.setEnum("BalanceWhiteAuto", "Off");
        }
        for (const auto& [channel, ratio] :
             { std::pair<const char*, double>{ "Red", red }, std::pair<const char*, double>{ "Green", green },
               std::pair<const char*, double>{ "Blue", blue } }) {
            genicam_.setEnum("BalanceRatioSelector", channel);
            genicam_.setFloat("BalanceRatio", ratio);
        }
    } catch (const GenicamException& e) {
        RCLCPP_ERROR_STREAM(LOGGER, "An exception while setting the white balance occurred: " << e.what());
        return e.what();
    }
    return "done";
}

std::string BaslerCamera::setBalanceWhiteAuto(const int& mode) {
    if (mode < 0 || mode >= static_cast<int>(BALANCE_WHITE_AUTO.size())) {
        return "Error: unknown value";
    }
    return setFeature("BalanceWhiteAuto", rclcpp::ParameterValue(BALANCE_WHITE_AUTO[mode]));
}

std::string BaslerCamera::setGrabbingStrategy(const int& strategy) {
    if (strategy < 0 || strategy > 2) {
        return "Unknown grabbing strategy";
    }
    grab_strategy_ = strategy;
    return "done";
}

std::string BaslerCamera::setMaxNumBuffer(const int& size) {
    if (size < 1) {
        return "requested maximum number of buffers must be at least 1";
    }
    max_num_buffer_ = size;
    return "done";
}

std::string BaslerCamera::setOutputQueueSize(const int& size) {
    if (size < 0 || size > max_num_buffer_) {
        return "requested output queue size is out side the limits of : 0-" + std::to_string(max_num_buffer_);
    }
    output_queue_size_ = size;
    return "done";
}

std::string BaslerCamera::setMaxTransferSize(const int& max_transfer_size) {
    if (!arv_camera_is_uv_device(camera_)) {
        return NOT_SUPPORTED;
    }
    if (max_transfer_size < 0) {
        return "Error: the maximum transfer size can't be negative";
    }
    max_transfer_size_ = static_cast<guint64>(max_transfer_size);
    return "done";
}

void BaslerCamera::setTimeouts(int grab_timeout_ms, int trigger_timeout_ms) {
    grab_timeout_ = grab_timeout_ms;
    trigger_timeout_ = trigger_timeout_ms;
}

std::string BaslerCamera::executeSoftwareTrigger() {
    if (!genicam_.isAvailable("TriggerSoftware")) {
        return NOT_SUPPORTED;
    }
    try {
        genicam_.execute("TriggerSoftware");
    } catch (const GenicamException& e) {
        return e.what();
    }
    return "done";
}

std::string BaslerCamera::saveUserSet(const std::string& user_set) {
    if (!genicam_.isAvailable("UserSetSelector") || !genicam_.isAvailable("UserSetSave")) {
        return NOT_SUPPORTED;
    }
    try {
        genicam_.setEnum("UserSetSelector", user_set);
        genicam_.execute("UserSetSave");
    } catch (const GenicamException& e) {
        return e.what();
    }
    return "done";
}

std::string BaslerCamera::loadUserSet(const std::string& user_set) {
    if (!genicam_.isAvailable("UserSetSelector") || !genicam_.isAvailable("UserSetLoad")) {
        return NOT_SUPPORTED;
    }
    const bool was_grabbing = is_grabbing_;
    try {
        grabbingStopping();
        genicam_.setEnum("UserSetSelector", user_set);
        genicam_.execute("UserSetLoad");
        updateImageSize();
    } catch (const GenicamException& e) {
        if (was_grabbing) {
            grabbingStarting();
        }
        return e.what();
    }
    return was_grabbing ? grabbingStarting() : "done";
}

std::string BaslerCamera::triggerDeviceReset() {
    if (!genicam_.isAvailable("DeviceReset")) {
        return NOT_SUPPORTED;
    }
    try {
        grabbingStopping();
        genicam_.execute("DeviceReset");
    } catch (const GenicamException& e) {
        return e.what();
    }
    return "done";
}

std::string BaslerCamera::loadPfs(const std::string& file_name) {
    std::ifstream file(file_name);
    if (!file) {
        return "Error: unable to read " + file_name;
    }
    std::vector<std::pair<std::string, std::string>> settings;
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }
        const std::size_t tab = line.find('\t');
        if (tab == std::string::npos) {
            continue;
        }
        settings.emplace_back(line.substr(0, tab), line.substr(tab + 1));
    }

    const bool was_grabbing = is_grabbing_;
    grabbingStopping();
    // Features constrain each other (Width bounds OffsetX, ...), so retry the ones that fail until a
    // pass makes no more progress, as GenApi's feature persistence does.
    std::string last_error;
    for (std::size_t pass = 0; pass < 3 && !settings.empty(); ++pass) {
        std::vector<std::pair<std::string, std::string>> failed;
        for (const auto& [feature, value] : settings) {
            try {
                if (genicam_.isWritable(feature)) {
                    genicam_.setValueFromString(feature, value);
                }
            } catch (const GenicamException& e) {
                last_error = feature + ": " + e.what();
                failed.emplace_back(feature, value);
            }
        }
        if (failed.size() == settings.size()) {
            break;
        }
        settings = std::move(failed);
    }
    try {
        updateImageSize();
    } catch (const GenicamException& e) {
        last_error = e.what();
    }
    if (was_grabbing) {
        grabbingStarting();
    }
    return settings.empty() ? "done" : "Error: unable to load " + last_error;
}

int BaslerCamera::getStatisticTotalBufferCount() const {
    guint64 completed, failures, underruns;
    currentStreamStatistics(completed, failures, underruns);
    return static_cast<int>(completed_buffers_ + completed + failed_buffers_ + failures);
}

int BaslerCamera::getStatisticFailedBufferCount() const {
    guint64 completed, failures, underruns;
    currentStreamStatistics(completed, failures, underruns);
    return static_cast<int>(failed_buffers_ + failures);
}

int BaslerCamera::getStatisticBufferUnderrunCount() const {
    guint64 completed, failures, underruns;
    currentStreamStatistics(completed, failures, underruns);
    return static_cast<int>(underruns_ + underruns);
}

int BaslerCamera::getStatisticMissedFrameCount() const { return static_cast<int>(missed_frames_); }

float BaslerCamera::getTemperature() const {
    return static_cast<float>(numberOr(firstAvailable({ "DeviceTemperature", "TemperatureAbs" }), 0.0, 0.0));
}

}  // namespace basler_driver
