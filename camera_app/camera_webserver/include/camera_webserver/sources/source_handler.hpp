#pragma once

#include <basin/web_core/web_node.hpp>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <map>

#include "camera_webserver/sources/source_base.hpp"

namespace camera_web::sources {

struct CameraSourceHandlerConfig {
    std::filesystem::path config_dir{ "/etc/app/camera_sources" };     ///< Directory for camera source configs
    std::filesystem::path calibration_dir{ "/etc/app/calibrations" };  ///< Directory for calibration files
};

using OnSrcUpdateCallback = std::function<void(sources::VideoSourceKey, bool, bool)>;

class CameraSourceHandler {
   public:
    CameraSourceHandler(std::shared_ptr<basin::web_core::WebNode> web_node,
                        OnSrcUpdateCallback on_src_update, CameraSourceHandlerConfig config);

    /// @brief List configured video sources. This includes all enabled and all connected sources.
    /// @return vector of VideoSourceConfigs for each source
    std::vector<sources::VideoSourceConfig> listSources();

    /// @brief Get available calibration files in the calibration directory
    /// @return vector of calibration file names in the calibration directory
    std::vector<std::string> getAvailableCalibrationFiles();

    /// @brief Update the configuration for a specific video source
    /// @param config VideoSourceConfig with updated settings
    bool updateSourceConfig(const sources::VideoSourceConfig& config);

   private:
    /// @brief Create directory if it does not exist
    /// @param dir_path Path to the directory
    void createDirIfNotExists(const std::filesystem::path& dir_path);

    /// @brief Load configured sources from config files
    /// @return vector of VideoSourceConfigs
    std::vector<sources::VideoSourceConfig> loadConfiguredSources() const;

    /// @brief Callback function for source updates
    /// @param source_key Key of the video source
    /// @param enabled Whether the source is enabled or disabled
    /// @param initial_load Whether this is the initial load of the source
    OnSrcUpdateCallback on_source_update_;  ///< Callback for source updates

    std::map<sources::CameraType, std::shared_ptr<sources::CameraSource>> available_source_types_;  ///< Available camera source types

    std::map<sources::VideoSourceKey, sources::VideoSourceConfig> sources_;  ///< Map of video sources

    CameraSourceHandlerConfig config_;

    rclcpp::Logger logger_{ rclcpp::get_logger("CameraSourceHandler") };
};

}  // namespace camera_web::sources
