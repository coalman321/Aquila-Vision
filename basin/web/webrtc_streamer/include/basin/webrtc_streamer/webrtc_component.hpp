#pragma once

#include <gst/gst.h>

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <string>
#include <thread>

namespace basin::webrtc_streamer {

// Bandwidth configuration structure
struct BandwidthConfig {
    int target_bitrate;       // Target bitrate in bps (e.g., 2000000 for 2 Mbps)
    int max_framerate;        // Maximum framerate (e.g., 30)
    double resolution_scale;  // Resolution scale factor (0.25 to 1.0)
    int encoder_speed;        // VP8 encoder speed (0-16, higher = faster/lower quality)

    BandwidthConfig() : target_bitrate(2000000), max_framerate(30), resolution_scale(1.0), encoder_speed(4) {}
};

// Forward declarations
typedef struct _GstElement GstElement;
typedef struct _GMainLoop GMainLoop;
typedef struct _GstPromise GstPromise;
typedef struct _GParamSpec GParamSpec;

class WebRTCComponent {
   public:
    using AnswerCallback = std::function<void(const std::string& sdp)>;

    explicit WebRTCComponent(rclcpp::Logger logger);
    ~WebRTCComponent();

    // Initialize the component (no longer creates pipeline)
    bool initialize();

    // Register a new stream source
    bool registerStream(const std::string& stream_id);

    // Unregister a stream source
    void unregisterStream(const std::string& stream_id);

    // Push image frame to a specific stream
    void pushFrame(const std::string& stream_id, const sensor_msgs::msg::Image::SharedPtr msg);

    // Handle WebRTC offer and generate answer for a specific stream (returns peer_id)
    std::string handleOffer(const std::string& stream_id, const std::string& sdp, AnswerCallback callback);

    // Disconnect a specific peer
    void disconnectPeer(const std::string& peer_id);

    // Get number of active peers
    size_t getActivePeerCount() const;

    // Get number of active peers for a specific stream
    size_t getActivePeerCount(const std::string& stream_id) const;

    // Get list of registered streams
    std::vector<std::string> getStreamIds() const;

    // Set bandwidth configuration for new connections
    void setDefaultBandwidthConfig(const BandwidthConfig& config);

    // Get current bandwidth configuration
    BandwidthConfig getDefaultBandwidthConfig() const;

    // Update encoder settings for a specific peer
    bool updatePeerBandwidth(const std::string& peer_id, const BandwidthConfig& config);

   private:
    // Per-stream pipeline data
    struct StreamSource {
        std::string stream_id;
        GstElement* pipeline;
        GstElement* appsrc;
        GstElement* tee;

        StreamSource() : pipeline(nullptr), appsrc(nullptr), tee(nullptr) {}
    };

    // Per-peer session data
    struct PeerSession {
        std::string peer_id;
        std::string stream_id;
        std::string codec_name;  // "VP8" or "VP9" - chosen during offer negotiation
        GstElement* webrtcbin;
        GstElement* tee_queue;
        GstElement* encoder;
        GstElement* rate_capsfilter;  // capsfilter after videorate; holds the fps cap
        AnswerCallback answer_callback;
        bool answer_sent;
        WebRTCComponent* component;  // back-pointer for use in static GStreamer callbacks

        PeerSession() : webrtcbin(nullptr), tee_queue(nullptr), encoder(nullptr), rate_capsfilter(nullptr), answer_sent(false), component(nullptr) {}
    };

    // GStreamer callbacks
    static void on_offer_created(GstPromise* promise, gpointer user_data);
    static void on_ice_gathering_state_notify(GstElement* webrtcbin, GParamSpec* pspec, gpointer user_data);
    static void on_ice_connection_state_notify(GstElement* webrtcbin, GParamSpec* pspec, gpointer user_data);
    static void on_connection_state_notify(GstElement* webrtcbin, GParamSpec* pspec, gpointer user_data);
    static void on_ice_candidate(GstElement* webrtc, guint mlineindex, gchar* candidate, gpointer user_data);

    // Helper methods
    std::string generatePeerId();
    void cleanupPeerSession(const std::string& peer_id);
    void cleanupStreamSource(const std::string& stream_id);
    PeerSession* findPeerByWebrtcbin(GstElement* webrtcbin);
    // Safe deferred cleanup: GStreamer callbacks enqueue peer_ids here; they
    // are removed from the pipeline on the next pushFrame() call.
    void schedulePeerDisconnect(const std::string& peer_id);
    void drainPendingDisconnects();

    // Member variables
    rclcpp::Logger logger_;
    GMainLoop* loop_;
    std::thread gst_thread_;

    mutable std::mutex streams_mutex_;
    std::map<std::string, std::shared_ptr<StreamSource>> streams_;

    // Per-stream state tracking
    std::map<std::string, GstCaps*> stream_caps_;
    std::map<std::string, bool> stream_caps_set_;
    std::map<std::string, std::optional<std::chrono::steady_clock::time_point>> stream_last_push_time_;

    mutable std::mutex peers_mutex_;
    std::map<std::string, std::shared_ptr<PeerSession>> peers_;
    int peer_counter_;

    std::mutex pending_mutex_;
    std::vector<std::string> pending_disconnects_;

    BandwidthConfig bandwidth_config_;
    mutable std::mutex config_mutex_;
};

}  // namespace basin::webrtc_streamer
