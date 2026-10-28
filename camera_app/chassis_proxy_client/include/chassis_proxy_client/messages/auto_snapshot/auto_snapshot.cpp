#include "chassis_proxy_client/messages/auto_snapshot/auto_snapshot.hpp"

#include "chassis_proxy_client/messages/msg_common.hpp"

namespace proxy_client {

std::vector<uint8_t> AutoSnapshot::serialize(const AutoSnapshot& msg) {
    BinOStream buffer;
    buffer << msg.msg_id;
    buffer << msg.evt_name;
    buffer << msg.start_snapshot;

    return buffer.serialize();
}

AutoSnapshot AutoSnapshot::deserialize(const std::vector<uint8_t>& data) {
    AutoSnapshot msg;
    if (data.empty() || data.at(0) != msg.msg_id) {
        throw SerializationError("msg id mismatch ");
    }

    BinIStream buffer(data);
    buffer >> msg.msg_id;
    buffer >> msg.evt_name;
    buffer >> msg.start_snapshot;

    return msg;
}

}  // namespace proxy_client
