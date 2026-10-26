#include "basin/webrtc_streamer/webrtc_streamer_node.hpp"

#include <chrono>
#include <cstdio>
#include <string>

#include "basin/webrtc_streamer/webrtc_component.hpp"
#include "basin/webrtc_streamer/webrtc_server.hpp"
#include "basin/webrtc_streamer/webrtc_streamer_params.hpp"
#include "gstreamer_bundled/bundled_plugins.h"

WebRTCStreamer::WebRTCStreamer(const rclcpp::NodeOptions& options) : WebNode("webrtc_streamer_node", options) {
    image_callback_group_ = create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);

    // Start background discovery thread before setting up streams/server so
    // available_topics_ is populated before any createStreamForTopic calls.
    discovery_running_ = true;
    discovery_thread_ = std::thread(&WebRTCStreamer::discoveryLoop, this);
    // Load parameters
    params_ = std::make_unique<WebRTCStreamerParams>(this);

    // Create WebRTC component
    webrtc_component_ = std::make_shared<basin::webrtc_streamer::WebRTCComponent>(this->get_logger());

    if (!webrtc_component_->initialize()) {
        RCLCPP_ERROR(this->get_logger(), "Failed to initialize WebRTC component");
        return;
    }

    // Apply initial bandwidth configuration
    RCLCPP_INFO(this->get_logger(), "Bandwidth: %d bps, %d fps, %.2fx scale, speed %d", params_->config.target_bitrate,
                params_->config.max_framerate, params_->config.resolution_scale, params_->config.encoder_speed);
    webrtc_component_->setDefaultBandwidthConfig(params_->config);

    // Create WebRTC server with callbacks for on-demand stream creation
    webrtc_server_ = std::make_shared<basin::webrtc_streamer::WebRTCServer>(
        this, webrtc_component_,
        [this](const std::string& topic_name) { return this->createStreamForTopic(topic_name); },
        [this]() { return this->getAvailableTopics(); });
}

void WebRTCStreamer::imageCallback(const std::string& stream_id, const sensor_msgs::msg::Image::SharedPtr msg) {
    RCLCPP_DEBUG(get_logger(), "Recieved image for %s ", stream_id.c_str());
    webrtc_component_->pushFrame(stream_id, msg);
}

void WebRTCStreamer::discoveryLoop() {
    while (discovery_running_) {
        if (webrtc_component_) {
            auto topic_names_and_types = this->get_topic_names_and_types();
            for (const auto& [topic_name, type_names] : topic_names_and_types) {
                bool is_image_topic = false;
                for (const auto& type_name : type_names) {
                    if (type_name == "sensor_msgs/msg/Image") { is_image_topic = true; break; }
                }
                if (!is_image_topic) continue;

                bool matches_pattern = false;
                for (const auto& pattern : params_->discovery_patterns) {
                    if (topic_name.find(pattern) != std::string::npos) { matches_pattern = true; break; }
                }
                if (!matches_pattern) continue;

                std::lock_guard<std::mutex> lk(topics_mutex_);
                if (available_topics_.count(topic_name) == 0) {
                    RCLCPP_INFO(this->get_logger(), "Discovered new image topic: %s", topic_name.c_str());
                    available_topics_.insert(topic_name);
                }
            }
        }
        // Sleep 1s between discovery sweeps — long enough to be completely
        // out of the way of 10-30Hz image delivery.
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

WebRTCStreamer::~WebRTCStreamer() {
    discovery_running_ = false;
    if (discovery_thread_.joinable()) discovery_thread_.join();
}

std::vector<std::string> WebRTCStreamer::getAvailableTopics() const {
    std::lock_guard<std::mutex> lk(topics_mutex_);
    return std::vector<std::string>(available_topics_.begin(), available_topics_.end());
}

bool WebRTCStreamer::createStreamForTopic(const std::string& topic_name) {
    // Check if topic is in our available topics list
    {
        std::lock_guard<std::mutex> lk(topics_mutex_);
        if (available_topics_.count(topic_name) == 0) {
            RCLCPP_WARN(this->get_logger(), "Topic '%s' is not in available topics list", topic_name.c_str());
            return false;
        }
    }

    // Check if we already have a subscription for this topic
    if (image_subs_.count(topic_name) > 0) {
        RCLCPP_DEBUG(this->get_logger(), "Stream for topic '%s' already exists", topic_name.c_str());
        return true;  // Already created
    }

    // Use topic name as stream_id
    std::string stream_id = topic_name;

    // Register stream in WebRTC component
    if (!webrtc_component_->registerStream(stream_id)) {
        RCLCPP_ERROR(this->get_logger(), "Failed to register stream '%s'", stream_id.c_str());
        return false;
    }

    // reliable keep_last=1: compatible with any reliable publisher, but DDS
    // only buffers 1 frame at a time — burst frames are discarded at the
    // DDS layer before the callback, without a QoS mismatch.
    rclcpp::SubscriptionOptions sub_opts;
    sub_opts.callback_group = image_callback_group_;
    image_subs_[stream_id] = this->create_subscription<sensor_msgs::msg::Image>(
        topic_name, rclcpp::QoS(1),
        [this, stream_id](const sensor_msgs::msg::Image::SharedPtr msg) { this->imageCallback(stream_id, msg); },
        sub_opts);

    RCLCPP_INFO(this->get_logger(), "Created on-demand stream '%s' on topic: %s", stream_id.c_str(),
                topic_name.c_str());

    return true;
}

int main(int argc, char** argv) {
    // The GStreamer plugins are built by @gstreamer and live in this binary's runfiles;
    // this has to run before the WebRTC component calls gst_init(). Without runfiles GStreamer keeps
    // looking where it always does, so a host installation still works.
    std::string plugin_error;
    if (!gstreamer_bundled::UseBundledPlugins(argv[0], &plugin_error)) {
        fprintf(stderr, "Using the system's GStreamer plugins (%s)\n", plugin_error.c_str());
    }

    rclcpp::init(argc, argv);
    auto node = std::make_shared<WebRTCStreamer>(rclcpp::NodeOptions());

    // Start the web server
    node->startServer();

    // Use MultiThreadedExecutor so image callbacks (image_callback_group_)
    // run on their own thread and are never blocked by timer callbacks.
    rclcpp::executors::MultiThreadedExecutor executor(
        rclcpp::ExecutorOptions(), 2);
    executor.add_node(node);
    executor.spin();
    rclcpp::shutdown();
    return 0;
}
