#pragma once

#include <basin/node_core/node_params.hpp>

namespace camera_web {

struct CameraWebParams : public basin::node_core::NodeParams {
    std::string app_dir{ "/tmp/app" };                  ///< Application storage directory
    std::string models_dir{ "models" };                 ///< sub-directory for model files
    std::string calibrations_dir{ "calibrations" };     ///< sub-directory for calibration files
    std::string source_config_dir{ "camera_sources" };  ///< sub-directory for camera source configs
    std::string pipeline_config_dir{ "pipelines" };     ///< sub-directory for pipeline configs
    std::string static_content_dir{ "static_content" };         ///< sub-directory for static content to serve

    bool enable_static_content{ true };  ///< status flag for enabling serving static content

    std::string field_map_base_frame{ "map" };    ///< Base frame for field position transforms
    std::string field_map_child_prefix{ "fp_" };  ///< Prefix for field position child frames

    std::string camera_base_frame{ "base_link" };  ///< Base frame for camera transforms

    std::string log_dir{ "/var/lib/aquila/logs" };  ///< The continuous logger's log directory, for snapshot downloads

    // basin::webrtc_streamer::BandwidthConfig bandwidth_config;  ///< WebRTC bandwidth configuration

    CameraWebParams(rclcpp::Node* node) : basin::node_core::NodeParams(node) {
        createParam("app_dir", app_dir);
        createParam("models_dir", models_dir);
        createParam("calibrations_dir", calibrations_dir);
        createParam("source_config_dir", source_config_dir);
        createParam("pipeline_config_dir", pipeline_config_dir);
        createParam("static_content_dir", static_content_dir);
        createParam("enable_static_content", enable_static_content);

        createParam("field.base_frame", field_map_base_frame);
        createParam("field.child_prefix", field_map_child_prefix);

        createParam("camera.base_frame", camera_base_frame);

        createParam("log_dir", log_dir);
    }
};

}  // namespace camera_web
