#include "chassis_proxy_client/messages/tag_detection/tag_detection.hpp"

#include <tf2/LinearMath/Matrix3x3.hpp>
#include <tf2/LinearMath/Quaternion.hpp>

#include "chassis_proxy_client/messages/msg_common.hpp"

namespace proxy_client {

void TagDetectionMsg::loadFromMsg(const std::vector<uint8_t>& detected_tag_ids, const geometry_msgs::msg::Pose& tag_pose_in_base,
                                  const TimeStamp& stamp, const std::string& cam_serial) {
    timestamp = stamp;
    tag_ids = detected_tag_ids;
    camera_serial = cam_serial;

    // convert from quaternion to euler angles in degrees
    tf2::Quaternion q(tag_pose_in_base.orientation.x, tag_pose_in_base.orientation.y, tag_pose_in_base.orientation.z,
                      tag_pose_in_base.orientation.w);
    tf2::Matrix3x3 m(q);
    double roll, pitch, yaw;
    m.getRPY(roll, pitch, yaw);

    theta_pos = yaw;
    x_pos = tag_pose_in_base.position.x;
    y_pos = tag_pose_in_base.position.y;
}

std::vector<uint8_t> TagDetectionMsg::serialize(const TagDetectionMsg& msg) {
    BinOStream buffer;
    buffer << msg.msg_id;
    buffer << msg.timestamp;

    // camera serial number
    buffer << msg.camera_serial;

    // pose
    buffer << msg.x_pos;
    buffer << msg.y_pos;
    buffer << msg.theta_pos;

    // tag ids
    int32_t tag_count = static_cast<int32_t>(msg.tag_ids.size());
    buffer << tag_count;
    for (const auto& tag_id : msg.tag_ids) {
        buffer << tag_id;
    }

    return buffer.serialize();
}

TagDetectionMsg TagDetectionMsg::deserialize(const std::vector<uint8_t>& data) {
    BinIStream buffer(data);
    TagDetectionMsg msg;

    if (data.empty() || data.at(0) != msg.msg_id) {
        throw SerializationError("msg id mismatch ");
    }

    buffer >> msg.msg_id;
    buffer >> msg.timestamp;

    // camera serial number
    buffer >> msg.camera_serial;

    // pose
    buffer >> msg.x_pos;
    buffer >> msg.y_pos;
    buffer >> msg.theta_pos;

    // tag ids
    int32_t tag_count = 0;
    buffer >> tag_count;
    for (int32_t i = 0; i < tag_count; ++i) {
        uint8_t tag_id;
        buffer >> tag_id;
        msg.tag_ids.push_back(tag_id);
    }

    return msg;
}

}  // namespace proxy_client
