#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace proxy_client {

struct ProxyOdomMsg {
    // Unique message identifier
    const uint8_t msg_id{ 30u };

    // Timestamp
    int32_t sec{ 0 };
    int32_t nanosec{ 0 };

    // Position data
    int32_t x_pos;
    int32_t y_pos;
    int32_t theta_pos;

    // Position variances for certainty estimation
    int32_t x_var;
    int32_t y_var;
    int32_t theta_var;

    // Velocity data
    int32_t x_dot;
    int32_t y_dot;
    int32_t theta_dot;

    static std::vector<uint8_t> serialize(const ProxyOdomMsg& msg);

    static ProxyOdomMsg deserialize(const std::vector<uint8_t>& data);
};

}  // namespace proxy_client
