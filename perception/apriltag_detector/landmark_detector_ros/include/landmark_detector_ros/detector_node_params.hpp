#pragma once

#include <basin/node_core/node_params.hpp>
#include <string>
#include <vector>

namespace landmark_detector {

struct DetectorNodeParams : public basin::node_core::NodeParams {
    // Connection settings
    std::string camera_topic{ "camera" };

    // Transform tracking settings
    std::string tracked_frame_prefix{ "tag_" };
    std::string parent_frame{ "map" };

    // Detection settings
    bool reload_needed{ false };
    bool enabled{ true };
    bool is_multitag{ false };
    double tag_edge_length{ 0.1651 };

    // Covariance settings
    std::vector<double> detect_covar{};
    double const_cov_gain{ 1.0 };
    double linear_cov_gain{ 0.0 };
    double cubic_cov_gain{ 0.0 };

    DetectorNodeParams(rclcpp::Node* node) : NodeParams(node) {
        // Connection settings
        createParam("camera_topic", camera_topic);

        // Transform tracking settings
        createDynParamCb("tracked_frame_prefix", tracked_frame_prefix, [this]() { reload_needed = true; });
        createDynParamCb("parent_frame", parent_frame, [this]() { reload_needed = true; });

        // Detection settings
        createDynParamCb("enabled", enabled, [this]() { reload_needed = true; });
        createDynParamCb("tag_edge_length", tag_edge_length, [this]() { reload_needed = true; });
        createDynParamCb("is_multitag", is_multitag, [this]() { reload_needed = true; });

        // Covariance settings
        createDynParam("detect_covar", detect_covar);
        createDynParam("const_cov_gain", const_cov_gain);
        createDynParam("linear_cov_gain", linear_cov_gain);
        createDynParam("cubic_cov_gain", cubic_cov_gain);
    }
};

}  // namespace landmark_detector
