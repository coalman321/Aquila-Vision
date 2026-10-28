#pragma once

#include <tf2_ros/buffer.hpp>
#include <tf2_ros/transform_listener.hpp>

#include <basin/node_core/node_core.hpp>
#include <basin/node_core/node_params.hpp>
#include <future>
#include <geometry_msgs/msg/pose_stamped.hpp>
#include <localization_msgs/msg/tag_solution.hpp>
#include <logging_msgs/msg/log_metadata.hpp>
#include <logging_msgs/srv/snapshot_request.hpp>
#include <memory>
#include <nav_msgs/msg/odometry.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <tf2_ros/buffer.hpp>
#include <tf2_ros/transform_listener.hpp>
#include <vision_msgs/msg/detection2_d_array.hpp>

#include "chassis_proxy_client/messages/auto_snapshot/auto_snapshot.hpp"
#include "chassis_proxy_client/messages/clock_sync/sync_response.hpp"
#include "chassis_proxy_client/messages/match_info/match_info.hpp"
#include "chassis_proxy_client/udp_server.hpp"
#include "logging_msgs/srv/snapshot_request.hpp"

namespace proxy_client {

struct ProxyClientNodeParams : public basin::node_core::NodeParams {
    int server_port{ 5809 };                 ///< Server port running on the roborio to connect to
    int client_port{ 5809 };                 ///< Server port running on the local device for the roborio to send data
    std::string server_addr{ "127.0.0.1" };  ///< Server address for the roborio

    std::string detections_topic{ "/vision/detections" };      ///< vision detections topic
    std::string camera_info_topic{ "/vision/camera_info" };    ///< vision detections topic
    std::string tag_solution_topic{ "/vision/tag_solution" };  ///< tag solution topic

    ///< TF frame of the robot base, used for transforming tag solutions to the correct frame
    std::string robot_base_frame{ "base_frame" };
    ///< TF frame of the field, used for transforming tag solutions to the correct frame
    std::string field_frame{ "field" };

    std::vector<std::string> class_names{};

    ProxyClientNodeParams(rclcpp::Node* node) : basin::node_core::NodeParams(node) {
        createParam("server_addr", server_addr);
        createParam("server_port", server_port);
        createParam("client_port", client_port);

        createParam("detections_topic", detections_topic);
        createParam("camera_info_topic", camera_info_topic);
        createParam("tag_solution_topic", tag_solution_topic);

        createParam("robot_base_frame", robot_base_frame);
        createParam("field_frame", field_frame);
    }
};

class ProxyClientNode : public basin::node_core::NodeCore {
   public:
    ProxyClientNode();

    void onTick() override;

    void shutdown() override;

    void tryConnectUdpServer();

    void processRecievedPackets();

    void detectionsCb(vision_msgs::msg::Detection2DArray::ConstSharedPtr msg);

    void camInfoCb(sensor_msgs::msg::CameraInfo::ConstSharedPtr msg);

    void tagSolutionCb(localization_msgs::msg::TagSolution::ConstSharedPtr msg);

    void matchInfoCb(MatchInfo msg);

    void autosnapCb(AutoSnapshot msg);

    void syncResponseCb(SyncResponseMsg msg);

   private:
    std::unique_ptr<ProxyClientNodeParams> params_;

    std::unique_ptr<UdpServer> udp_server_;

    sensor_msgs::msg::CameraInfo::ConstSharedPtr cam_info_;  ///< Camera info message received last

    rclcpp::Subscription<vision_msgs::msg::Detection2DArray>::SharedPtr detections_sub_;
    rclcpp::Subscription<sensor_msgs::msg::CameraInfo>::SharedPtr cam_info_sub_;
    rclcpp::Subscription<localization_msgs::msg::TagSolution>::SharedPtr tag_solution_sub_;

    ///< Client for requesting snapshots from the logging system
    rclcpp::Client<logging_msgs::srv::SnapshotRequest>::SharedPtr snapshot_client_;
    ///< Future for the snapshot response, set when a request is sent and waited on in the main loop
    std::shared_future<logging_msgs::srv::SnapshotRequest::Response::SharedPtr> snapshot_resp_;

    ///< Publisher for sending log metadata to the logging system
    rclcpp::Publisher<logging_msgs::msg::LogMetadata>::SharedPtr log_metadata_pub_;

    ///< Publisher for sending tag pose in base frame
    rclcpp::Publisher<geometry_msgs::msg::PoseStamped>::SharedPtr tag_pose_pub_;

    double clock_offset_sec_{ 0.0 };  ///< Estimated offset between local clock and server clock in seconds

    rclcpp::Time last_sync_time_;  ///< Last time a sync request was sent

    ///< TF2 buffer for caching transforms
    std::unique_ptr<tf2_ros::Buffer> tf_buffer_;
    ///< TF2 listener for receiving transforms
    std::shared_ptr<tf2_ros::TransformListener> tf_listener_;
};
}  // namespace proxy_client
