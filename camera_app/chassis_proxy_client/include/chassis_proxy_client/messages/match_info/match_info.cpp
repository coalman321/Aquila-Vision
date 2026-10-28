#include "chassis_proxy_client/messages/match_info/match_info.hpp"

#include "chassis_proxy_client/messages/msg_common.hpp"

namespace proxy_client {

std::vector<uint8_t> MatchInfo::serialize(const MatchInfo& msg) {
    BinOStream buffer;
    buffer << msg.msg_id;
    buffer << msg.match_num;
    buffer << msg.station;
    buffer << msg.match_type;
    buffer << msg.evt_name;

    return buffer.serialize();
}

MatchInfo MatchInfo::deserialize(const std::vector<uint8_t>& data) {
    MatchInfo msg;
    if (data.empty() || data.at(0) != msg.msg_id) {
        throw SerializationError("msg id mismatch ");
    }

    BinIStream buffer(data);
    buffer >> msg.msg_id;
    buffer >> msg.match_num;
    buffer >> msg.station;
    buffer >> msg.match_type;
    buffer >> msg.evt_name;

    return msg;
}

}  // namespace proxy_client
