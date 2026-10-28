#pragma once

#include <cstdint>
#include <vector>
#include "chassis_proxy_client/messages/msg_common.hpp"

namespace proxy_client {

struct SyncResponseMsg {
    // Unique message identifier
    const uint8_t msg_id{ 61u };

    // Request identifier echoed back
    int32_t req_id{ 0 };

    // Server timestamps: when request was received and when response sent
    TimeStamp server_stamp;

    // Client timestamp when request was sent (echoed back for RTT calculation)
    TimeStamp client_send_stamp;

    static std::vector<uint8_t> serialize(const SyncResponseMsg& msg);

    static SyncResponseMsg deserialize(const std::vector<uint8_t>& data);
};

}  // namespace proxy_client
