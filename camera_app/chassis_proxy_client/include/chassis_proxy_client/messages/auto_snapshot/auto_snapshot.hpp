#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace proxy_client {

struct AutoSnapshot {
    // Unique message identifier
    const uint8_t msg_id{ 52u };

    std::string evt_name;

    bool start_snapshot{ false };

    static std::vector<uint8_t> serialize(const AutoSnapshot& msg);

    static AutoSnapshot deserialize(const std::vector<uint8_t>& data);
};

}  // namespace proxy_client
