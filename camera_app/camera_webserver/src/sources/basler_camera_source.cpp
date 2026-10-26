#include "camera_webserver/sources/basler_camera_source.hpp"

#include <arv.h>

#include <rclcpp/logging.hpp>
#include <unordered_map>
#include <utility>

#include "camera_webserver/sources/source_base.hpp"

namespace camera_web::sources {

BaslerCameraSource::BaslerCameraSource() : logger_(rclcpp::get_logger("BaslerCameraSource")) {}

// Static lookup table mapping Basler camera model names to their maximum resolutions
static const std::unordered_map<std::string, std::pair<int, int>> MODEL_RESOLUTIONS = {
    // ace Classic (GigE/USB)
    { "acA720-520um", { 720, 540 } },
    { "acA720-520uc", { 720, 540 } },
    { "acA1300-60gm", { 1280, 1024 } },
    { "acA1300-60gc", { 1280, 1024 } },
    { "acA1300-75gm", { 1280, 1024 } },
    { "acA1300-75gc", { 1280, 1024 } },
    { "acA1920-40um", { 1920, 1200 } },
    { "acA1920-40uc", { 1920, 1200 } },
    { "acA1920-50gm", { 1920, 1200 } },
    { "acA1920-50gc", { 1920, 1200 } },
    { "acA2440-75um", { 2448, 2048 } },
    { "acA2440-75uc", { 2448, 2048 } },
    { "acA2500-14gm", { 2592, 1944 } },
    { "acA2500-14gc", { 2592, 1944 } },
    { "acA2500-20gm", { 2592, 1944 } },
    { "acA2500-20gc", { 2592, 1944 } },
    { "acA3088-16gm", { 3088, 2076 } },
    { "acA3088-16gc", { 3088, 2076 } },
    { "acA3800-14um", { 3840, 2160 } },
    { "acA3800-14uc", { 3840, 2160 } },
    { "acA4024-8gm", { 4024, 3036 } },
    { "acA4024-8gc", { 4024, 3036 } },
    { "acA4112-20um", { 4096, 3000 } },
    { "acA4112-20uc", { 4096, 3000 } },
    { "acA5472-17um", { 5472, 3648 } },
    { "acA5472-17uc", { 5472, 3648 } },

    // ace 2 (GigE/USB)
    { "a2A1920-51gmBAS", { 1920, 1200 } },
    { "a2A1920-51gcBAS", { 1920, 1200 } },
    { "a2A1920-160umBAS", { 1920, 1200 } },
    { "a2A1920-160ucBAS", { 1920, 1200 } },
    { "a2A2440-98umBAS", { 2448, 2048 } },
    { "a2A2440-98ucBAS", { 2448, 2048 } },
    { "a2A2448-23gmBAS", { 2448, 2048 } },
    { "a2A2448-23gcBAS", { 2448, 2048 } },
    { "a2A2448-75umBAS", { 2448, 2048 } },
    { "a2A2448-75ucBAS", { 2448, 2048 } },
    { "a2A2590-22gmBAS", { 2592, 1944 } },
    { "a2A2590-22gcBAS", { 2592, 1944 } },
    { "a2A2590-60umBAS", { 2592, 1944 } },
    { "a2A2590-60ucBAS", { 2592, 1944 } },
    { "a2A2600-20gmBAS", { 2592, 2048 } },
    { "a2A2600-20gcBAS", { 2592, 2048 } },
    { "a2A2600-64umBAS", { 2592, 2048 } },
    { "a2A2600-64ucBAS", { 2592, 2048 } },
    { "a2A3840-13gmBAS", { 3840, 2160 } },
    { "a2A3840-13gcBAS", { 3840, 2160 } },
    { "a2A3840-45umBAS", { 3840, 2160 } },
    { "a2A3840-45ucBAS", { 3840, 2160 } },
    { "a2A4096-9gmBAS", { 4096, 3000 } },
    { "a2A4096-9gcBAS", { 4096, 3000 } },
    { "a2A4096-30umBAS", { 4096, 3000 } },
    { "a2A4096-30ucBAS", { 4096, 3000 } },
    { "a2A4200-12gmBAS", { 4200, 3000 } },
    { "a2A4200-12gcBAS", { 4200, 3000 } },
    { "a2A4504-5gmBAS", { 4504, 4504 } },
    { "a2A4504-5gcBAS", { 4504, 4504 } },
    { "a2A4504-18umBAS", { 4504, 4504 } },
    { "a2A4504-18ucBAS", { 4504, 4504 } },
    { "a2A5320-7gmBAS", { 5320, 4600 } },
    { "a2A5320-7gcBAS", { 5320, 4600 } },
    { "a2A5320-23umBAS", { 5320, 4600 } },
    { "a2A5320-23ucBAS", { 5320, 4600 } },

    // dart (USB 3.0)
    { "daA1280-54um", { 1280, 1024 } },
    { "daA1280-54uc", { 1280, 1024 } },
    { "daA1600-60um", { 1600, 1200 } },
    { "daA1600-60uc", { 1600, 1200 } },
    { "daA1920-30um", { 1920, 1200 } },
    { "daA1920-160um", { 1920, 1200 } },
    { "daA1920-30uc", { 1920, 1200 } },
    { "daA2448-70um", { 2448, 2048 } },
    { "daA2448-70uc", { 2448, 2048 } },
    { "daA2500-14um", { 2592, 1944 } },
    { "daA2500-14uc", { 2592, 1944 } },
    { "daA3840-45um", { 3840, 2160 } },
    { "daA3840-45uc", { 3840, 2160 } },
};

std::string BaslerCameraSource::getSourceTypeName() const { return "BASLER"; }

bool BaslerCameraSource::isSourceConnected(const std::string& name) {
    std::set<VideoSourceConfig> connected_sources = listConnectedSources();

    for (const auto& source : connected_sources) {
        if (source.camera_name == name) {
            return true;
        }
    }

    return false;
}

std::set<VideoSourceConfig> BaslerCameraSource::listConnectedSources() {
    std::set<VideoSourceConfig> connected_sources;

    // Aravis finds GigE Vision and USB3 Vision cameras without opening them; Basler's are the ones
    // whose vendor says so.
    arv_update_device_list();
    const unsigned int n_devices = arv_get_n_devices();

    for (unsigned int i = 0; i < n_devices; ++i) {
        const char* vendor = arv_get_device_vendor(i);
        const char* serial = arv_get_device_serial_nbr(i);
        const char* model = arv_get_device_model(i);
        if (vendor == nullptr || serial == nullptr || model == nullptr ||
            std::string(vendor).find("Basler") == std::string::npos) {
            continue;
        }

        std::string camera_name = serial;
        std::string model_name = model;

        // Lookup resolution from model name
        int width = 0;
        int height = 0;
        auto it = MODEL_RESOLUTIONS.find(model_name);
        if (it != MODEL_RESOLUTIONS.end()) {
            width = it->second.first;
            height = it->second.second;
            RCLCPP_DEBUG(logger_, "Model %s resolution: %dx%d", model_name.c_str(), width, height);
        } else {
            RCLCPP_WARN(logger_, "Unknown model %s, using default resolution 1920x1200", model_name.c_str());
            width = 1920;
            height = 1200;
        }

        // Create VideoSourceConfig for the connected camera
        VideoSourceConfig source_config;
        source_config.camera_type = CameraType::BASLER;
        source_config.camera_name = camera_name;
        source_config.source_connected = true;
        source_config.base_width = width;
        source_config.base_height = height;

        connected_sources.emplace(source_config);

        RCLCPP_INFO(logger_, "Added camera: %s (Model: %s, Resolution: %dx%d)", camera_name.c_str(),
                    model_name.c_str(), width, height);
    }

    if (connected_sources.empty()) {
        RCLCPP_WARN(logger_, "No Basler cameras found");
    } else {
        RCLCPP_INFO(logger_, "Found %zu Basler camera(s)", connected_sources.size());
    }

    return connected_sources;
}

}  // namespace camera_web::sources
