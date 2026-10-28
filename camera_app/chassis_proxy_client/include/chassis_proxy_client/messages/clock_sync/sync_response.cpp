#include "chassis_proxy_client/messages/clock_sync/sync_response.hpp"

#include "chassis_proxy_client/messages/msg_common.hpp"

namespace proxy_client {

std::vector<uint8_t> SyncResponseMsg::serialize(const SyncResponseMsg& msg) {
    BinOStream buffer;
    buffer << msg.msg_id;
    buffer << msg.req_id;
    buffer << msg.server_stamp;
    buffer << msg.client_send_stamp;

    return buffer.serialize();
}

SyncResponseMsg SyncResponseMsg::deserialize(const std::vector<uint8_t>& data) {
    SyncResponseMsg msg;

    if (data.empty() || data.at(0) != msg.msg_id) {
        throw SerializationError("msg id mismatch ");
    }

    BinIStream buffer(data);
    buffer >> msg.msg_id;
    buffer >> msg.req_id;
    buffer >> msg.server_stamp;
    buffer >> msg.client_send_stamp;

    return msg;
} 

}  // namespace proxy_client
