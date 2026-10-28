#pragma once

#include <arv.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <rclcpp/parameter_value.hpp>
#include <rclcpp/time.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/region_of_interest.hpp>
#include <string>
#include <utility>
#include <vector>

#include "basler_driver/basler_driver_parameters.hpp"
#include "basler_driver/binary_exposure_search.hpp"
#include "basler_driver/genicam.hpp"

namespace basler_driver {

enum class OpenByType { FirstFound, DeviceUserID, DeviceSerial };

// Where a selector-dependent feature lives: `selector` is set to `value` before the feature is read
// or written (e.g. LineSelector=Line2 for LineMode). Empty for features that aren't selected.
struct FeatureSelection {
    std::string selector;
    std::string value;
};

// A Basler USB3 Vision camera, driven through Aravis rather than Pylon. Follows the camera layer of
// pylon-ros-camera (PylonROS2USBCamera): actions return "done" or an error message.
//
// Not thread safe: BaslerDriverNode serializes every call through its grab mutex.
class BaslerCamera {
   public:
    // Opens the first matching USB3 Vision device, or returns nullptr. FirstFound only considers
    // Basler devices.
    static std::unique_ptr<BaslerCamera> create(OpenByType open_by_type, const std::string& id);

    ~BaslerCamera();
    BaslerCamera(const BaslerCamera&) = delete;
    BaslerCamera& operator=(const BaslerCamera&) = delete;

    // Pylon's CSoftwareTriggerConfiguration: continuous acquisition, every frame started by a software
    // trigger the node fires at frame_rate.
    bool applySoftwareTriggerConfiguration();
    // Loads startup_user_set and sets the auto function limits.
    bool applyCamSpecificStartupSettings(const BaslerDriverParameters& parameters);
    // Sets shutter mode, image encoding and stream settings, starts acquisition and grabs one frame to
    // check it works.
    bool startGrabbing(const BaslerDriverParameters& parameters);

    bool isReady() const { return is_ready_; }
    bool isCamRemoved() const { return removed_; }
    bool isGrabbing() const { return is_grabbing_; }

    // Grabs one frame into `image` (triggering it if the camera is in software trigger mode). `stamp`
    // is replaced by the frame's chunk timestamp when chunk mode with the Timestamp chunk is on.
    bool grab(std::vector<uint8_t>& image, rclcpp::Time& stamp);

    std::string grabbingStarting();
    std::string grabbingStopping();

    // Generic access for the node's feature parameters. Bool values map onto On/Off enumerations.
    std::string setFeature(const std::string& feature, const rclcpp::ParameterValue& value,
                           const FeatureSelection& selection = {});
    // The feature's current value as `type`, or nullopt if the camera can't read it.
    std::optional<rclcpp::ParameterValue> getFeature(const std::string& feature, rclcpp::ParameterType type,
                                                     const FeatureSelection& selection = {});
    bool hasFeature(const std::string& feature) const { return genicam_.isAvailable(feature); }
    // The entries of an enumeration feature the camera offers (empty if it has no such feature).
    std::vector<std::string> featureEntries(const std::string& feature) const;

    const std::string& deviceUserID() const { return device_user_id_; }
    const std::size_t& imageRows() const { return img_rows_; }
    const std::size_t& imageCols() const { return img_cols_; }
    // Bytes per pixel.
    int imagePixelDepth() const { return pixel_depth_; }
    std::string currentROSEncoding() const;
    std::string currentBaslerEncoding() const { return pixel_format_; }
    std::vector<std::string> detectAvailableImageEncodings(const bool& show_message);
    std::string setImageEncoding(const std::string& ros_encoding);

    float maxPossibleFramerate() const;
    void getInitialCameraInfo(sensor_msgs::msg::CameraInfo& cam_info_msg) const;

    float currentExposure() const;
    // Fraction (0..1) of the camera's gain range.
    float currentGain() const;
    float currentGamma() const;
    std::size_t currentBinningX() const;
    std::size_t currentBinningY() const;
    std::size_t currentOffsetX() const;
    std::size_t currentOffsetY() const;
    sensor_msgs::msg::RegionOfInterest currentROI() const;

    bool setExposure(const float& target_exposure, float& reached_exposure);
    bool setGain(const float& target_gain, float& reached_gain);
    bool setGamma(const float& target_gamma, float& reached_gamma);
    // A zero width or height means the full sensor.
    bool setROI(const sensor_msgs::msg::RegionOfInterest& target_roi, sensor_msgs::msg::RegionOfInterest& reached_roi);
    bool setBinningX(const std::size_t& target_binning_x, std::size_t& reached_binning_x);
    bool setBinningY(const std::size_t& target_binning_y, std::size_t& reached_binning_y);
    std::string setShutterMode(const SHUTTER_MODE& shutter_mode);
    std::string setAutoExposureUpperLimit(const double& upper_limit);

    // One step of the brightness search the node runs: starts the camera's "Once" auto functions when
    // the target is within their range, else advances a binary search over the exposure time.
    bool setBrightness(const int& target_brightness, const float& current_brightness, const bool& exposure_auto,
                       const bool& gain_auto);
    bool isAutoBrightnessFunctionRunning() const;
    void disableAllRunningAutoBrightessFunctions();
    void enableContinuousAutoExposure();
    void enableContinuousAutoGain();
    float maxBrightnessTolerance() const { return 2.5f; }

    std::string setWhiteBalance(const double& red, const double& green, const double& blue);
    // 0 = Off, 1 = Once, 2 = Continuous.
    std::string setBalanceWhiteAuto(const int& mode);

    // Applied the next time grabbing starts (the node restarts it).
    std::string setGrabbingStrategy(const int& strategy);
    std::string setMaxNumBuffer(const int& size);
    std::string setOutputQueueSize(const int& size);
    std::string setMaxTransferSize(const int& max_transfer_size);
    void setTimeouts(int grab_timeout_ms, int trigger_timeout_ms);
    int getMaxNumBuffer() const { return max_num_buffer_; }

    std::string executeSoftwareTrigger();
    std::string saveUserSet(const std::string& user_set);
    std::string loadUserSet(const std::string& user_set);
    std::string triggerDeviceReset();

    // Loads a feature persistence (.pfs) file of "Name\tValue" lines, as Pylon Viewer saves them.
    std::string loadPfs(const std::string& file_name);

    // Stream statistics over every stream this camera has had.
    int getStatisticTotalBufferCount() const;
    int getStatisticFailedBufferCount() const;
    int getStatisticBufferUnderrunCount() const;
    int getStatisticMissedFrameCount() const;

    float getTemperature() const;

   private:
    explicit BaslerCamera(ArvCamera* camera);

    // The first of `names` the camera has; empty if none.
    std::string firstAvailable(std::initializer_list<const char*> names) const;
    std::string exposureFeature() const;
    std::string gainFeature() const;
    void select(const FeatureSelection& selection) const;

    // Value of a numeric feature, `not_available` when the camera lacks it and `error` when reading fails.
    double numberOr(const std::string& feature, double not_available, double error) const;

    void updateImageSize();
    bool isSoftwareTriggered() const;
    bool waitForFrameTriggerReady();
    // Pops the next buffer according to the grab strategy; the caller must recycle() it.
    ArvBuffer* retrieveBuffer(guint64 timeout_us);
    void recycle(ArvBuffer* buffer);
    void destroyStream();
    void currentStreamStatistics(guint64& completed, guint64& failures, guint64& underruns) const;
    bool setExtendedBrightness(const int& target_brightness, const float& current_brightness);

    static void onControlLost(ArvDevice* device, gpointer user_data);

    ArvCamera* camera_;
    Genicam genicam_;
    ArvStream* stream_ = nullptr;
    ArvChunkParser* chunk_parser_ = nullptr;
    std::deque<ArvBuffer*> pending_;
    bool is_grabbing_ = false;
    bool is_ready_ = false;
    std::atomic<bool> removed_{ false };

    std::string device_user_id_;
    std::size_t img_rows_ = 0;
    std::size_t img_cols_ = 0;
    std::size_t img_size_byte_ = 0;
    // PixelFormat and its bytes per pixel, cached since grab() needs them for every frame.
    std::string pixel_format_;
    int pixel_depth_ = 0;
    std::vector<std::string> available_image_encodings_;

    // 0 = OneByOne, 1 = LatestImageOnly, 2 = LatestImages.
    int grab_strategy_ = 0;
    int grab_timeout_ = 500;
    int trigger_timeout_ = 5000;
    int max_num_buffer_ = 10;
    int output_queue_size_ = 1;
    guint64 max_transfer_size_ = 0;

    // Statistics of streams already destroyed.
    guint64 completed_buffers_ = 0;
    guint64 failed_buffers_ = 0;
    guint64 underruns_ = 0;
    guint64 missed_frames_ = 0;
    guint64 last_frame_id_ = 0;
    bool has_last_frame_id_ = false;

    std::unique_ptr<BinaryExposureSearch> binary_exp_search_;
};

}  // namespace basler_driver
