#pragma once

#include <basin/node_core/node_params.hpp>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <vector>

#include "basin/webrtc_streamer/webrtc_component.hpp"

struct WebRTCStreamerParams : public basin::node_core::NodeParams {
    // Bandwidth configuration
    basin::webrtc_streamer::BandwidthConfig config;

    // Topic discovery patterns
    std::vector<std::string> discovery_patterns;
    // Topics containing any of these substrings are skipped even if they match a pattern
    std::vector<std::string> discovery_exclude_patterns;

    WebRTCStreamerParams(rclcpp::Node* node) : basin::node_core::NodeParams(node) {
        // set default values for bandwidth config
        config.target_bitrate = 2000000;  // 2 Mbps default
        config.max_framerate = 30;
        config.resolution_scale = 1.0;
        config.encoder_speed = 4;  // 0=best quality, 16=fastest

        // Default discovery patterns
        discovery_patterns = {"/driver/image_raw", "/debug_img", "/debug_image"};
        // Internal buffer-transport sub-topics (e.g. /debug_image/_buf_cpu)
        discovery_exclude_patterns = {"/_buf_"};

        createParam("bandwidth.target_bitrate", config.target_bitrate);
        createParam("bandwidth.max_framerate", config.max_framerate);
        createParam("bandwidth.resolution_scale", config.resolution_scale);
        createParam("bandwidth.encoder_speed", config.encoder_speed);

        createParam("discovery.patterns", discovery_patterns);
        createParam("discovery.exclude_patterns", discovery_exclude_patterns);
    }
};

