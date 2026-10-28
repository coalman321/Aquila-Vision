#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace proxy_client {

struct ProxyVisionDetection {
    // Unique message identifier
    const uint8_t msg_id{ 10u };

    // Timestamp
    int32_t sec{ 0 };
    int32_t nanosec{ 0 };

    // Camera serial number / identifier
    std::string camera_serial;

    int32_t detection_count;  ///< Number of detections to expect in total
    int32_t detection_idx;    ///< Index of the detection

    uint8_t class_id;  ///< Class identifier #

    double theta_x;  ///< Angle from camera center on x axis
    double theta_y;  ///< Angle from camera center on y axis

    static std::vector<uint8_t> serialize(const ProxyVisionDetection& msg);

    static ProxyVisionDetection deserialize(const std::vector<uint8_t>& data);
};

}  // namespace proxy_client
