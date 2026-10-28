#include "chassis_proxy_client/proxy_client.hpp"

#include <tf2/LinearMath/Matrix3x3.hpp>
#include <tf2/LinearMath/Transform.hpp>
#include <yaml-cpp/yaml.h>

#include <iomanip>
#include <limits>
#include <tf2_geometry_msgs/tf2_geometry_msgs.hpp>

#include "chassis_proxy_client/time_util.hpp"

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
        switch (udp_server_->processPackets()) {
            case UdpServer::ReceiveResult::kMalformed:
                RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                                     "Dropped a proxy packet that is not a valid Envelope");
                break;
            case UdpServer::ReceiveResult::kVersionMismatch:
                RCLCPP_WARN_THROTTLE(get_logger(), *get_clock(), 5000,
                                     "Dropped a proxy packet with an unsupported protocol_version (expected %d); "
                                     "is the robot running the same MW-Lib release this build pins?",
                                     v1::PROTOCOL_VERSION_CURRENT);
                break;
            case UdpServer::ReceiveResult::kUnhandled:
                RCLCPP_DEBUG(get_logger(), "Ignored a proxy packet with no registered handler");
                break;
            default:
                break;
        }
    } catch (const std::runtime_error& e) {
        RCLCPP_WARN_STREAM(get_logger(), "Failed to handle proxy packet with error: " << e.what());

        // try to recover by reconnecting the server
        tryConnectUdpServer();
    }

    // Periodically send clock sync requests to the server to maintain an accurate estimate of the clock offset
    const rclcpp::Time now = get_clock()->now();
    if (now - last_sync_time_ > rclcpp::Duration(5s)) {
        v1::Envelope envelope;
        v1::TimesyncRequest* sync_req = envelope.mutable_timesync_request();

        RCLCPP_INFO(get_logger(), "Sending clock sync request to server");

        *sync_req->mutable_client_send() = toProto(get_clock()->now());
        udp_server_->send(envelope);
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
    v1::Envelope envelope;
    v1::TagSolution* proxy_msg = envelope.mutable_tag_solution();
    *proxy_msg->mutable_stamp() = offsetBy(toProto(msg->header.stamp), clock_offset_sec_);
    proxy_msg->set_camera_serial(msg->header.frame_id);
    proxy_msg->mutable_tag_ids()->Add(msg->detected_tags.begin(), msg->detected_tags.end());

    double roll, pitch, yaw;
    tf2::Matrix3x3(base_pose_in_field.getRotation()).getRPY(roll, pitch, yaw);
    v1::Pose2d* robot_pose = proxy_msg->mutable_robot_pose();
    robot_pose->set_x_m(base_pose.position.x);
    robot_pose->set_y_m(base_pose.position.y);
    robot_pose->set_theta_rad(yaw);

    udp_server_->send(envelope);
}

void ProxyClientNode::matchInfoCb(const v1::MatchInfo& msg) {
    RCLCPP_INFO_STREAM(get_logger(), "Received Match Info for event: " << msg.event_name());

    YAML::Emitter emitter;
    emitter << YAML::BeginMap;
    emitter << YAML::Key << "event_name" << YAML::Value << msg.event_name();
    emitter << YAML::Key << "match_num" << YAML::Value << msg.match_num();
    emitter << YAML::Key << "match_type" << YAML::Value << v1::MatchType_Name(msg.match_type());
    emitter << YAML::Key << "station" << YAML::Value << v1::AllianceStation_Name(msg.station());
    emitter << YAML::EndMap;

    logging_msgs::msg::LogMetadata meta_msg;
    meta_msg.key_name = "match_info";
    meta_msg.timed_event = false;
    meta_msg.yaml_metadata = emitter.c_str();

    log_metadata_pub_->publish(meta_msg);
}

void ProxyClientNode::autosnapCb(const v1::AutoSnapshot& msg) {
    RCLCPP_INFO_STREAM(get_logger(), "Received Snapshot Request: " << msg.event_name());

    const rclcpp::Time now = get_clock()->now();

    logging_msgs::srv::SnapshotRequest::Request::SharedPtr request =
        std::make_shared<logging_msgs::srv::SnapshotRequest::Request>();
    request->description = msg.event_name();
    request->snapshot_name = "autosnap_" + msg.event_name() + "_" + std::to_string(now.seconds());
    request->request_id = msg.start() ? request->REQUEST_START : request->REQUEST_STOP;

    snapshot_resp_ = snapshot_client_->async_send_request(request).share();
}

void ProxyClientNode::syncResponseCb(const v1::TimesyncResponse& msg) {
    const double client_time_at_recv = get_clock()->now().seconds();

    // recover the client and server time from the response
    const double client_at_send = toSec(msg.client_send());
    const double server_time = toSec(msg.server_recv());

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

    v1::Envelope envelope;
    v1::PieceDetections* proxy_msg = envelope.mutable_piece_detections();
    *proxy_msg->mutable_stamp() = offsetBy(toProto(msg->header.stamp), clock_offset_sec_);
    proxy_msg->set_camera_serial(msg->header.frame_id);

    if (!msg->detections.empty()) {
        // filter for greatest bbox area
        const vision_msgs::msg::Detection2D* best = &msg->detections.front();
        double best_area = best->bbox.size_x * best->bbox.size_y;
        for (const vision_msgs::msg::Detection2D& detection : msg->detections) {
            double area = detection.bbox.size_x * detection.bbox.size_y;

            if (area > best_area) {
                best = &detection;
                best_area = area;
            }
        }

        // Try to find a class id match and force to unknown if unknown
        std::vector<std::string>::iterator it =
            std::find(params_->class_names.begin(), params_->class_names.end(), best->id);
        if (it == params_->class_names.end()) {
            it = (params_->class_names.end() - 1);
        }

        // now convert it to the packet
        v1::Detection* detection = proxy_msg->add_detections();
        detection->set_class_id(std::distance(params_->class_names.begin(), it));

        // theta = atan((x - cx) / fx)
        detection->set_theta_x_rad(atan((best->bbox.center.position.x - cam_info_->k.at(2)) / cam_info_->k.at(0)));
        detection->set_theta_y_rad(atan((best->bbox.center.position.y - cam_info_->k.at(5)) / cam_info_->k.at(4)));
    }

    udp_server_->send(envelope);
}

void ProxyClientNode::tryConnectUdpServer() {
    // reset existing client and server if they exist
    udp_server_.reset();

    try {
        RCLCPP_INFO(get_logger(), "Binding local UDP port %d for proxy communication", params_->client_port);
        RCLCPP_INFO(get_logger(), "Connecting to %s:%d", params_->server_addr.c_str(), params_->server_port);

        // Now setup the server to listen for incoming packets from the proxy server
        udp_server_ = std::make_unique<UdpServer>(params_->client_port, params_->server_addr, params_->server_port);
        udp_server_->registerHandler(v1::Envelope::kMatchInfo,
                                     [this](const v1::Envelope& envelope) { matchInfoCb(envelope.match_info()); });
        udp_server_->registerHandler(v1::Envelope::kAutoSnapshot,
                                     [this](const v1::Envelope& envelope) { autosnapCb(envelope.auto_snapshot()); });
        udp_server_->registerHandler(v1::Envelope::kTimesyncResponse, [this](const v1::Envelope& envelope) {
            syncResponseCb(envelope.timesync_response());
        });
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
