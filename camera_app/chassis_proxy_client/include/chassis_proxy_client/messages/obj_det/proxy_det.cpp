#include "chassis_proxy_client/messages/obj_det/proxy_det.hpp"

#include "chassis_proxy_client/messages/msg_common.hpp"

namespace proxy_client {

std::vector<uint8_t> ProxyVisionDetection::serialize(const ProxyVisionDetection& msg) {
    BinOStream buffer;
    buffer << msg.msg_id;
    buffer << msg.sec;
    buffer << msg.nanosec;
    buffer << msg.camera_serial;
    buffer << msg.detection_count;
    buffer << msg.detection_idx;
    buffer << msg.class_id;
    buffer << msg.theta_x;
    buffer << msg.theta_y;

    return buffer.serialize();
}

ProxyVisionDetection ProxyVisionDetection::deserialize(const std::vector<uint8_t>& data) {
    ProxyVisionDetection msg;
    if (data.empty() || data.at(0) != msg.msg_id) {
        throw SerializationError("msg id mismatch ");
    }

    BinIStream buffer(data);
    buffer >> msg.msg_id;
    buffer >> msg.sec;
    buffer >> msg.nanosec;
    buffer >> msg.camera_serial;
    buffer >> msg.detection_count;
    buffer >> msg.detection_idx;
    buffer >> msg.class_id;
    buffer >> msg.theta_x;
    buffer >> msg.theta_y;

    return msg;
} 

}  // namespace proxy_client
