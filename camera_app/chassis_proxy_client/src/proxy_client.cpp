#include "chassis_proxy_client/proxy_client.hpp"

#include <tf2/LinearMath/Transform.hpp>
#include <yaml-cpp/yaml.h>

#include <iomanip>
#include <limits>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>

#include "chassis_proxy_client/messages/clock_sync/sync_request.hpp"
#include "chassis_proxy_client/messages/obj_det/proxy_det.hpp"
#include "chassis_proxy_client/messages/tag_detection/tag_detection.hpp"

using namespace std::placeholders;
using namespace std::chrono_literals;

namespace proxy_client {

ProxyClientNode::ProxyClientNode() : basin::node_core::NodeCore("proxy_client") {
    params_ = std::make_unique<ProxyClientNodeParams>(this);

    // Always force an Unknown ID to the end of the class list
    params_->class_names.push_back("Unknown");

    // Initialize TF2 buffer and listener
    tf_buffer_ = std::make_unique<tf2_ros::Buffer>(get_clock());
    tf_listener_ = std::make_shared<tf2_ros::TransformListener>(*tf_buffer_);

    detections_sub_ = create_subscription<vision_msgs::msg::Detection2DArray>(
        params_->detections_topic, rclcpp::SensorDataQoS(), std::bind(&ProxyClientNode::detectionsCb, this, _1));
    cam_info_sub_ = create_subscription<sensor_msgs::msg::CameraInfo>(
        params_->camera_info_topic, rclcpp::SystemDefaultsQoS(), std::bind(&ProxyClientNode::camInfoCb, this, _1));

    tag_solution_sub_ = create_subscription<localization_msgs::msg::TagSolution>(
        params_->tag_solution_topic, rclcpp::SystemDefaultsQoS(), std::bind(&ProxyClientNode::tagSolutionCb, this, _1));

    log_metadata_pub_ = create_publisher<logging_msgs::msg::LogMetadata>("/log/metadata", rclcpp::SystemDefaultsQoS());

    tag_pose_pub_ = create_publisher<geometry_msgs::msg::PoseStamped>("/vision/tag_pose", rclcpp::SystemDefaultsQoS());

    snapshot_client_ = create_client<logging_msgs::srv::SnapshotRequest>("/log/snapshot_request");

    tryConnectUdpServer();

    // initialize times
    last_sync_time_ = get_clock()->now();

    RCLCPP_INFO(get_logger(), "Proxy client startup complete");
}

void ProxyClientNode::onTick() {
    // Called periodically by the base class tick timer
    try {
        udp_server_->processPackets();
    } catch (const std::runtime_error& e) {
        RCLCPP_WARN_STREAM(get_logger(), "Failed to handle proxy packet with error: " << e.what());

        // try to recover by reconnecting the server
        tryConnectUdpServer();
    }

    // Periodically send clock sync requests to the server to maintain an accurate estimate of the clock offset
    const rclcpp::Time now = get_clock()->now();
    if (now - last_sync_time_ > rclcpp::Duration(5s)) {
        SyncRequestMsg sync_req;

        RCLCPP_INFO(get_logger(), "Sending clock sync request to server");

        sync_req.timestamp = TimeStamp::fromRosTime(get_clock()->now());
        udp_server_->sendMsg(sync_req);
        last_sync_time_ = now;

        RCLCPP_INFO(get_logger(), "Sent clock sync request to server");
    }

    // Check for snapshot response if a request was sent
    if (snapshot_resp_.valid() && snapshot_resp_.wait_for(0s) == std::future_status::ready) {
        auto response = snapshot_resp_.get();
        if (response) {
            RCLCPP_INFO_STREAM(get_logger(), "Snapshot response received: " << response->response_msg);
        } else {
            RCLCPP_ERROR_STREAM(get_logger(), "Snapshot request failed");
        }
        snapshot_resp_ = std::shared_future<logging_msgs::srv::SnapshotRequest::Response::SharedPtr>();
    }
}

void ProxyClientNode::tagSolutionCb(localization_msgs::msg::TagSolution::ConstSharedPtr msg) {
    RCLCPP_DEBUG_STREAM(get_logger(), "Received Tag Solution with " << msg->detected_tags.size() << " detections");

    // msg->pose is the camera pose in the field frame
    // We need to transform it to get the base pose in the field frame

    // Convert poses to tf2::Transform for proper transform math
    tf2::Transform camera_pose_in_field;
    tf2::fromMsg(msg->pose, camera_pose_in_field);

    // Look up the transform from base to camera (static transform describing camera mounting)
    tf2::Transform camera_to_base_tf;

    try {
        geometry_msgs::msg::TransformStamped cam_to_base;
        cam_to_base = tf_buffer_->lookupTransform(msg->header.frame_id, params_->robot_base_frame, rclcpp::Time(0),
                                                  rclcpp::Duration(10ms));
        tf2::fromMsg(cam_to_base.transform, camera_to_base_tf);
    } catch (const tf2::TransformException& e) {
        RCLCPP_WARN_STREAM(get_logger(), "Failed to get transform from " << params_->robot_base_frame << " to "
                                                                         << msg->header.frame_id << " : " << e.what());
        return;
    }
    // Calculate base pose in field: base_in_field = camera_in_field * camera_in_base
    tf2::Transform base_pose_in_field = camera_pose_in_field * camera_to_base_tf;

    // Convert back to geometry_msgs
    geometry_msgs::msg::Pose base_pose;
    tf2::toMsg(base_pose_in_field, base_pose);

    // Publish the base pose as a PoseStamped message
    geometry_msgs::msg::PoseStamped pose_msg;
    pose_msg.header.stamp = msg->header.stamp;
    pose_msg.header.frame_id = params_->field_frame;
    pose_msg.pose = base_pose;
    tag_pose_pub_->publish(pose_msg);

    // now send it to the server
    TagDetectionMsg proxy_msg;
    proxy_msg.loadFromMsg(msg->detected_tags, base_pose,
                          TimeStamp::fromRosTime(msg->header.stamp).offsetBy(clock_offset_sec_),
                          msg->header.frame_id);
    udp_server_->sendMsg(proxy_msg);
}

void ProxyClientNode::matchInfoCb(MatchInfo msg) {
    // multiline
    RCLCPP_INFO_STREAM(get_logger(), "Received Match Info for event: " << msg.evt_name);

    YAML::Emitter emitter;
    emitter << YAML::BeginMap;
    emitter << YAML::Key << "event_name" << YAML::Value << msg.evt_name;
    emitter << YAML::Key << "match_num" << YAML::Value << msg.match_num;
    emitter << YAML::Key << "match_type" << YAML::Value << msg.match_type;
    emitter << YAML::Key << "station" << YAML::Value << msg.station;
    emitter << YAML::EndMap;

    logging_msgs::msg::LogMetadata meta_msg;
    meta_msg.key_name = "match_info";
    meta_msg.timed_event = false;
    meta_msg.yaml_metadata = emitter.c_str();

    log_metadata_pub_->publish(meta_msg);
}

void ProxyClientNode::autosnapCb(AutoSnapshot msg) {
    RCLCPP_INFO_STREAM(get_logger(), "Received Snapshot Request: " << msg.evt_name);

    const rclcpp::Time now = get_clock()->now();

    logging_msgs::srv::SnapshotRequest::Request::SharedPtr request =
        std::make_shared<logging_msgs::srv::SnapshotRequest::Request>();
    request->description = msg.evt_name;
    request->snapshot_name = "autosnap_" + msg.evt_name + "_" + std::to_string(now.seconds());
    request->request_id = msg.start_snapshot ? request->REQUEST_START : request->REQUEST_STOP;

    snapshot_resp_ = snapshot_client_->async_send_request(request).share();
}

void ProxyClientNode::syncResponseCb(SyncResponseMsg msg) {
    const double client_time_at_recv = get_clock()->now().seconds();

    // recover the client and server time from the response
    const double client_at_send = msg.client_send_stamp.toSec();
    const double server_time = msg.server_stamp.toSec();

    // compute the clock offset using the round trip time method
    const double half_rtt = (client_time_at_recv - client_at_send) / 2.0;
    clock_offset_sec_ = server_time - (client_at_send + half_rtt);

    RCLCPP_INFO_STREAM(get_logger(), std::setprecision(std::numeric_limits<double>::max_digits10)
                                         << "Computed new sync offset: " << clock_offset_sec_);
}

void ProxyClientNode::camInfoCb(sensor_msgs::msg::CameraInfo::ConstSharedPtr msg) { cam_info_ = msg; }

void ProxyClientNode::detectionsCb(vision_msgs::msg::Detection2DArray::ConstSharedPtr msg) {
    if (!cam_info_) {
        RCLCPP_WARN(get_logger(), "Unable to process detections. Waiting for camera info");
        return;
    }

    if (msg->detections.empty()) {
        ProxyVisionDetection proxy_msg;
        proxy_msg.sec = msg->header.stamp.sec;
        proxy_msg.nanosec = msg->header.stamp.nanosec;
        proxy_msg.camera_serial = msg->header.frame_id;
        proxy_msg.detection_count = 0;
        udp_server_->sendMsg(proxy_msg);

    } else {
        // filter for greatest bbox area
        vision_msgs::msg::Detection2D best;
        double best_area = 0;
        for (const vision_msgs::msg::Detection2D& detection : msg->detections) {
            double area = detection.bbox.size_x * detection.bbox.size_y;

            if (area > best_area) {
                best = detection;
            }
        }

        // Try to find a class id match and force to unknown if unknown
        std::vector<std::string>::iterator it =
            std::find(params_->class_names.begin(), params_->class_names.end(), best.id);
        if (it == params_->class_names.end()) {
            it = (params_->class_names.end() - 1);
        }

        // now convert it to the packet
        uint8_t class_id = std::distance(params_->class_names.begin(), it);
        ProxyVisionDetection proxy_msg;
        proxy_msg.sec = msg->header.stamp.sec;
        proxy_msg.nanosec = msg->header.stamp.nanosec;
        proxy_msg.camera_serial = msg->header.frame_id;
        proxy_msg.detection_count = 1;
        proxy_msg.detection_idx = 0;
        proxy_msg.class_id = class_id;

        // theta = atan((x - cx) / fx)
        proxy_msg.theta_x =
            (atan((best.bbox.center.position.x - cam_info_->k.at(2)) / cam_info_->k.at(0)) / M_PI) * 180.0;
        proxy_msg.theta_y =
            (atan((best.bbox.center.position.y - cam_info_->k.at(5)) / cam_info_->k.at(4)) / M_PI) * 180.0;

        udp_server_->sendMsg(proxy_msg);
    }
}

void ProxyClientNode::tryConnectUdpServer() {
    // reset existing client and server if they exist
    udp_server_.reset();

    try {
        RCLCPP_INFO(get_logger(), "Binding local UDP port %d for proxy communication", params_->client_port);
        RCLCPP_INFO(get_logger(), "Connecting to %s:%d", params_->server_addr.c_str(), params_->server_port);

        // Now setup the server to listen for incoming packets from the proxy server
        udp_server_ = std::make_unique<UdpServer>(params_->client_port, params_->server_addr, params_->server_port);
        udp_server_->registerType<MatchInfo>(std::bind(&ProxyClientNode::matchInfoCb, this, _1));
        udp_server_->registerType<AutoSnapshot>(std::bind(&ProxyClientNode::autosnapCb, this, _1));
        udp_server_->registerType<SyncResponseMsg>(std::bind(&ProxyClientNode::syncResponseCb, this, _1));
    } catch (const std::system_error& e) {
        RCLCPP_ERROR_STREAM(get_logger(),
                            "Failed to connect to " << params_->server_addr << ":" << params_->server_port);
        RCLCPP_ERROR_STREAM(get_logger(), "Error: " << e.what());
    }
}

void ProxyClientNode::shutdown() {
    RCLCPP_INFO(get_logger(), "Proxy client shutting down");
    // Cleanup resources if needed
    udp_server_.reset();
}

}  // namespace proxy_client

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);

    std::shared_ptr<proxy_client::ProxyClientNode> node = std::make_shared<proxy_client::ProxyClientNode>();
    rclcpp::spin(node);

    rclcpp::shutdown();
}
