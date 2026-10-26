#pragma once

#include <basin/web_core/web_node.hpp>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <set>
#include <string>
#include <thread>

#include "basin/webrtc_streamer/webrtc_component.hpp"
#include "basin/webrtc_streamer/webrtc_server.hpp"
#include "basin/webrtc_streamer/webrtc_streamer_params.hpp"

class WebRTCStreamer : public basin::web_core::WebNode {
   public:
    explicit WebRTCStreamer(const rclcpp::NodeOptions& options);
    ~WebRTCStreamer() override;

    // onTick is intentionally empty — topic discovery runs on a background
    // thread to avoid DDS participant-lock contention with message delivery.
    void onTick() override {}

    // Get list of available image topics (discovered but not necessarily active streams)
    std::vector<std::string> getAvailableTopics() const;

    // Create stream on-demand for the given topic
    bool createStreamForTopic(const std::string& topic_name);

   private:
    // ROS callback (stream_id is captured in the lambda)
    void imageCallback(const std::string& stream_id, const sensor_msgs::msg::Image::SharedPtr msg);

    // Background thread that calls get_topic_names_and_types() every second.
    // Runs independently of the ROS executor so DDS discovery lock can never
    // delay image message delivery.
    void discoveryLoop();

    // Member variables
    std::unique_ptr<WebRTCStreamerParams> params_;
    std::shared_ptr<basin::webrtc_streamer::WebRTCComponent> webrtc_component_;
    std::shared_ptr<basin::webrtc_streamer::WebRTCServer> webrtc_server_;
    std::map<std::string, rclcpp::Subscription<sensor_msgs::msg::Image>::SharedPtr> image_subs_;

    mutable std::mutex topics_mutex_;
    std::set<std::string> available_topics_;

    std::atomic<bool> discovery_running_{false};
    std::thread discovery_thread_;

    // Image callbacks run in their own callback group so the heartbeat timer
    // can never stall frame delivery.
    rclcpp::CallbackGroup::SharedPtr image_callback_group_;
};
