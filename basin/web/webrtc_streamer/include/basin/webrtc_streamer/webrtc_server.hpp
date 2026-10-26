#pragma once

#include <basin/web_core/web_node.hpp>
#include <functional>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <vector>

namespace basin::webrtc_streamer {

// Forward declaration
class WebRTCComponent;

class WebRTCServer {
   public:
    using StreamCreationCallback = std::function<bool(const std::string&)>;
    using AvailableTopicsCallback = std::function<std::vector<std::string>()>;

    explicit WebRTCServer(basin::web_core::WebNode* node, std::shared_ptr<WebRTCComponent> webrtc_component,
                          StreamCreationCallback stream_creator, AvailableTopicsCallback available_topics_getter);
    ~WebRTCServer() = default;

   private:
    // Register HTTP endpoints
    void registerEndpoints(httplib::Server& server);

    // HTTP handlers
    void handleOffer(const httplib::Request& req, httplib::Response& res);
    void handleDisconnect(const httplib::Request& req, httplib::Response& res);
    void handleStatus(const httplib::Request& req, httplib::Response& res);
    void handleGetBandwidthConfig(const httplib::Request& req, httplib::Response& res);
    void handleSetBandwidthConfig(const httplib::Request& req, httplib::Response& res);
    void handleUpdatePeerBandwidth(const httplib::Request& req, httplib::Response& res);
    void handleListStreams(const httplib::Request& req, httplib::Response& res);
    void handleStreamStatus(const httplib::Request& req, httplib::Response& res);

    basin::web_core::WebNode* node_;
    std::shared_ptr<WebRTCComponent> webrtc_component_;
    StreamCreationCallback stream_creator_;
    AvailableTopicsCallback available_topics_getter_;
};

}  // namespace basin::webrtc_streamer
