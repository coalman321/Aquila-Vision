#include "chassis_proxy_client/messages/clock_sync/sync_request.hpp"

#include "chassis_proxy_client/messages/msg_common.hpp"

namespace proxy_client {

std::vector<uint8_t> SyncRequestMsg::serialize(const SyncRequestMsg& msg) {
    BinOStream buffer;
    buffer << msg.msg_id;
    buffer << msg.timestamp;
    buffer << msg.req_id;

    return buffer.serialize();
}

SyncRequestMsg SyncRequestMsg::deserialize(const std::vector<uint8_t>& data) {
    SyncRequestMsg msg;

    if (data.empty() || data.at(0) != msg.msg_id) {
        throw SerializationError("msg id mismatch ");
    }

    BinIStream buffer(data);
    buffer >> msg.msg_id;
    buffer >> msg.timestamp;
    buffer >> msg.req_id;

    return msg;
} 

}  // namespace proxy_client
