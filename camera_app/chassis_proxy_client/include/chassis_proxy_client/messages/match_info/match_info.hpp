#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace proxy_client {

struct MatchInfo {
    // Unique message identifier
    const uint8_t msg_id{ 50u };

    // Info on the station
    uint8_t match_num;
    uint8_t station;
    uint8_t match_type;
    std::string evt_name;

    static std::vector<uint8_t> serialize(const MatchInfo& msg);

    static MatchInfo deserialize(const std::vector<uint8_t>& data);
};

}  // namespace proxy_client
