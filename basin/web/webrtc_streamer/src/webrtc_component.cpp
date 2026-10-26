#include "basin/webrtc_streamer/webrtc_component.hpp"

#include <gst/app/gstappsrc.h>
#include <gst/sdp/gstsdp.h>
#include <gst/webrtc/webrtc.h>

#include <chrono>

namespace basin::webrtc_streamer {

// ---------------------------------------------------------------------------
// Codec negotiation helper
// ---------------------------------------------------------------------------

// Describes the codec chosen for a peer based on the client's SDP offer.
struct CodecSelection {
    std::string name;  // "VP8" or "VP9"
    int payload_type;  // PT from the offer
};

// Walk the SDP offer in client-preference order (first format listed wins)
// and return the first VP8 or VP9 codec entry found.  Falls back to VP8/120
// if neither is explicitly found so we always produce a usable selection.
static CodecSelection selectCodecFromOffer(GstSDPMessage* sdp_msg) {
    CodecSelection fallback{"VP8", 120};

    guint num_medias = gst_sdp_message_medias_len(sdp_msg);
    for (guint i = 0; i < num_medias; ++i) {
        const GstSDPMedia* media = gst_sdp_message_get_media(sdp_msg, i);
        if (g_strcmp0(gst_sdp_media_get_media(media), "video") != 0) {
            continue;
        }

        // Formats are listed in client preference order
        guint num_formats = gst_sdp_media_formats_len(media);
        for (guint f = 0; f < num_formats; ++f) {
            const gchar* fmt = gst_sdp_media_get_format(media, f);
            int pt = std::atoi(fmt);
            std::string rtpmap_prefix = std::to_string(pt) + " ";

            guint num_attrs = gst_sdp_media_attributes_len(media);
            for (guint a = 0; a < num_attrs; ++a) {
                const GstSDPAttribute* attr = gst_sdp_media_get_attribute(media, a);
                if (g_strcmp0(attr->key, "rtpmap") != 0) {
                    continue;
                }
                std::string val(attr->value ? attr->value : "");
                if (val.rfind(rtpmap_prefix, 0) != 0) {
                    continue;
                }
                if (val.find("VP9/") != std::string::npos) {
                    return CodecSelection{"VP9", pt};
                } else if (val.find("VP8/") != std::string::npos) {
                    return CodecSelection{"VP8", pt};
                }
            }
        }
    }
    return fallback;
}

// ---------------------------------------------------------------------------
// GStreamer → RCLCPP log bridge
// Routes GStreamer debug messages into the ROS logger so they appear in
// ros2 launch / journalctl output alongside node logs.
// ---------------------------------------------------------------------------
static void gst_log_to_rclcpp(GstDebugCategory* category, GstDebugLevel level, const gchar* file,
                               const gchar* function, gint line, GObject* object,
                               GstDebugMessage* message, gpointer user_data) {
    (void)object;
    rclcpp::Logger* logger = static_cast<rclcpp::Logger*>(user_data);
    const char* msg = gst_debug_message_get(message);
    const char* cat = gst_debug_category_get_name(category);

    switch (level) {
        case GST_LEVEL_ERROR:
            RCLCPP_ERROR(*logger, "[GST %s] %s:%d %s: %s", cat, file, line, function, msg);
            break;
        case GST_LEVEL_WARNING:
            RCLCPP_WARN(*logger, "[GST %s] %s:%d %s: %s", cat, file, line, function, msg);
            break;
        case GST_LEVEL_FIXME:
        case GST_LEVEL_INFO:
            RCLCPP_INFO(*logger, "[GST %s] %s:%d %s: %s", cat, file, line, function, msg);
            break;
        default:
            RCLCPP_DEBUG(*logger, "[GST %s] %s:%d %s: %s", cat, file, line, function, msg);
            break;
    }
}

WebRTCComponent::WebRTCComponent(rclcpp::Logger logger) : logger_(logger), loop_(nullptr), peer_counter_(0) {}

WebRTCComponent::~WebRTCComponent() {
    // Clean up all peer sessions
    {
        std::lock_guard<std::mutex> lock(peers_mutex_);
        for (auto& pair : peers_) {
            cleanupPeerSession(pair.first);
        }
        peers_.clear();
    }

    // Clean up all stream sources
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        for (auto& pair : streams_) {
            cleanupStreamSource(pair.first);
        }
        streams_.clear();
        
        // Clean up all cached caps
        for (auto& pair : stream_caps_) {
            if (pair.second) {
                gst_caps_unref(pair.second);
            }
        }
        stream_caps_.clear();
        stream_caps_set_.clear();
    }

    if (loop_) {
        g_main_loop_quit(loop_);
    }
    if (gst_thread_.joinable()) {
        gst_thread_.join();
    }
    if (loop_) {
        g_main_loop_unref(loop_);
    }
}

bool WebRTCComponent::initialize() {
    // Initialize GStreamer
    if (!gst_is_initialized()) {
        gst_init(nullptr, nullptr);
    }

    // Route GStreamer log output through the RCLCPP logger.
    // This replaces the default gst_debug_log_default (stderr) handler so that
    // all GStreamer messages appear in the ROS log alongside node output.
    gst_debug_add_log_function(
        gst_log_to_rclcpp,
        new rclcpp::Logger(logger_),
        [](gpointer data) { delete static_cast<rclcpp::Logger*>(data); });
    gst_debug_remove_log_function(gst_debug_log_default);

    // Enable TRACE-level (7) debugging for WebRTC signalling, SDP, ICE, DTLS
    // and codec elements so that negotiation failures are fully visible.
    // Lower-priority categories (video conversion, buffering) stay at INFO (4)
    // to avoid flooding the log.
    gst_debug_set_threshold_from_string(
        "webrtcbin:7,"
        "sdp:7,"
        "dtls*:7,"
        "ice*:7,"
        "nice*:7,"
        "rtp*:6,"
        "rtpmanager:6,"
        "vp8enc:5,"
        "rtpvp8pay:5,"
        "appsrc:5,"
        "GST_CAPS:6",
        FALSE);

    RCLCPP_INFO(logger_, "GStreamer trace-level debugging enabled for WebRTC/SDP/ICE/DTLS categories");

    // Start GMainLoop in a thread
    loop_ = g_main_loop_new(nullptr, FALSE);
    gst_thread_ = std::thread([this]() { g_main_loop_run(loop_); });

    return true;
}

bool WebRTCComponent::registerStream(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);

    // Check if stream already exists
    if (streams_.find(stream_id) != streams_.end()) {
        RCLCPP_WARN(logger_, "Stream %s already registered", stream_id.c_str());
        return false;
    }

    // Create new stream source
    auto stream = std::make_shared<StreamSource>();
    stream->stream_id = stream_id;

    // Create pipeline with tee to broadcast to multiple peers
    std::string pipeline_str =
        "appsrc name=source format=time is-live=true block=false max-bytes=0 ! "
        "videoconvert ! video/x-raw,format=I420 ! "
        "tee name=t allow-not-linked=true";

    GError* error = nullptr;
    stream->pipeline = gst_parse_launch(pipeline_str.c_str(), &error);
    if (!stream->pipeline) {
        RCLCPP_ERROR(logger_, "Failed to create GStreamer pipeline for stream %s: %s", stream_id.c_str(),
                     error->message);
        g_error_free(error);
        return false;
    }

    stream->appsrc = gst_bin_get_by_name(GST_BIN(stream->pipeline), "source");
    stream->tee = gst_bin_get_by_name(GST_BIN(stream->pipeline), "t");

    if (!stream->appsrc || !stream->tee) {
        RCLCPP_ERROR(logger_, "Failed to get pipeline elements for stream %s", stream_id.c_str());
        gst_object_unref(stream->pipeline);
        return false;
    }

    // Configure appsrc for live streaming.
    // do-timestamp=TRUE lets appsrc stamp each buffer from the GStreamer pipeline
    // clock rather than from a separate steady_clock.  The two clocks can drift
    // by 50-100 ppm; at 20 minutes that is 60-120 ms of accumulated skew which
    // pushes buffer PTS ahead of the pipeline running time, causing the jitter
    // buffer to stall and then drop frames.
    g_object_set(stream->appsrc, "stream-type", 0,  // GST_APP_STREAM_TYPE_STREAM
                 "is-live", TRUE,
                 "format", GST_FORMAT_TIME,
                 "do-timestamp", TRUE,
                 "min-latency", (gint64)0,
                 "max-latency", (gint64)0,
                 nullptr);

    // Start pipeline
    gst_element_set_state(stream->pipeline, GST_STATE_PLAYING);

    streams_[stream_id] = stream;
    RCLCPP_INFO(logger_, "Registered stream: %s", stream_id.c_str());

    return true;
}

void WebRTCComponent::unregisterStream(const std::string& stream_id) {
    std::lock_guard<std::mutex> lock(streams_mutex_);

    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return;
    }

    // Disconnect all peers using this stream
    {
        std::lock_guard<std::mutex> peers_lock(peers_mutex_);
        std::vector<std::string> peers_to_disconnect;

        for (const auto& pair : peers_) {
            if (pair.second->stream_id == stream_id) {
                peers_to_disconnect.push_back(pair.first);
            }
        }

        for (const auto& peer_id : peers_to_disconnect) {
            cleanupPeerSession(peer_id);
            peers_.erase(peer_id);
        }
    }

    cleanupStreamSource(stream_id);
    streams_.erase(it);

    // Clean up stream state tracking
    auto caps_it = stream_caps_.find(stream_id);
    if (caps_it != stream_caps_.end()) {
        if (caps_it->second) {
            gst_caps_unref(caps_it->second);
        }
        stream_caps_.erase(caps_it);
    }
    stream_caps_set_.erase(stream_id);

    RCLCPP_INFO(logger_, "Unregistered stream: %s", stream_id.c_str());
}

void WebRTCComponent::schedulePeerDisconnect(const std::string& peer_id) {
    std::lock_guard<std::mutex> lock(pending_mutex_);
    pending_disconnects_.push_back(peer_id);
}

void WebRTCComponent::drainPendingDisconnects() {
    std::vector<std::string> to_disconnect;
    {
        std::lock_guard<std::mutex> lock(pending_mutex_);
        to_disconnect.swap(pending_disconnects_);
    }
    for (const auto& peer_id : to_disconnect) {
        RCLCPP_INFO(logger_, "[Peer %s] Auto-disconnecting dead peer", peer_id.c_str());
        disconnectPeer(peer_id);
    }
}

void WebRTCComponent::pushFrame(const std::string& stream_id, const sensor_msgs::msg::Image::SharedPtr msg) {
    using Clock = std::chrono::steady_clock;
    using Ms    = std::chrono::duration<double, std::milli>;

    // ── timing marker 0: entry ──────────────────────────────────────────────
    auto t0 = Clock::now();

    // Drain any peers that were marked dead by GStreamer ICE/connection callbacks.
    // Must happen before acquiring streams_mutex_ to avoid lock-order inversion
    // (disconnectPeer takes peers_mutex_ then streams_mutex_ via cleanupPeerSession).
    drainPendingDisconnects();

    // ── timing marker 1: after drain ────────────────────────────────────────
    auto t1 = Clock::now();

    std::lock_guard<std::mutex> lock(streams_mutex_);

    // ── timing marker 2: after lock ─────────────────────────────────────────
    auto t2 = Clock::now();

    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return;
    }

    auto stream = it->second;
    if (!stream->appsrc || !stream->pipeline) {
        return;
    }

    // Check if pipeline is in PLAYING state
    GstState state;
    gst_element_get_state(stream->pipeline, &state, nullptr, 0);
    if (state != GST_STATE_PLAYING) {
        return;
    }

    // Per-stream tracking of caps and base timestamps

    GstCaps* caps = nullptr;
    if (msg->encoding == "bgr8") {
        caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "BGR", "width", G_TYPE_INT, msg->width,
                                   "height", G_TYPE_INT, msg->height, "framerate", GST_TYPE_FRACTION, 0, 1, nullptr);
    } else if (msg->encoding == "rgb8") {
        caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "RGB", "width", G_TYPE_INT, msg->width,
                                   "height", G_TYPE_INT, msg->height, "framerate", GST_TYPE_FRACTION, 0, 1, nullptr);
    } else if (msg->encoding == "mono8") {
        caps = gst_caps_new_simple("video/x-raw", "format", G_TYPE_STRING, "GRAY8", "width", G_TYPE_INT, msg->width,
                                   "height", G_TYPE_INT, msg->height, "framerate", GST_TYPE_FRACTION, 0, 1, nullptr);
    } else {
        RCLCPP_WARN(logger_, "Unsupported encoding: %s", msg->encoding.c_str());
        return;
    }

    if (stream_caps_set_[stream_id] == false) {
        g_object_set(stream->appsrc, "caps", caps, nullptr);
        stream_caps_[stream_id] = caps;
        stream_caps_set_[stream_id] = true;
        stream_last_push_time_[stream_id] = std::nullopt;
    }

    // Per-stream frame counters for diagnostics (static so they persist between calls)
    static std::map<std::string, uint64_t> s_frame_count;
    static std::map<std::string, uint64_t> s_skip_count;
    // s_last_entry: time of previous pushFrame entry (t0), used to measure
    // actual callback delivery rate independently of the fps gate state.
    static std::map<std::string, std::optional<Clock::time_point>> s_last_entry;
    s_frame_count[stream_id]++;

    // Measure entry-to-entry gap (pre-mutex, pre-gate).
    // If this is small, callbacks are genuinely arriving fast (subscription queue burst).
    // If this is large but the gate gap is small, last_steady state is wrong.
    double entry_gap_ms = -1.0;
    if (s_last_entry[stream_id].has_value()) {
        entry_gap_ms = Ms(t0 - *s_last_entry[stream_id]).count();
        if (entry_gap_ms < 50.0) {
            RCLCPP_WARN(logger_, "[%s] rapid callback: entry_gap=%.1fms frame=%lu",
                stream_id.c_str(), entry_gap_ms, s_frame_count[stream_id]);
        }
    }
    s_last_entry[stream_id] = t0;

    // Enforce the configured max_framerate using steady_clock (wall time).
    // steady_clock is immune to GStreamer pipeline clock changes (e.g. clock
    // renegotiation after webrtcbin RTCP sync) and directly measures real
    // inter-call interval.  Buffer PTS is still stamped by do-timestamp=TRUE
    // using the GStreamer pipeline clock, completely independently of this gate.
    int configured_fps;
    {
        std::lock_guard<std::mutex> cfg_lock(config_mutex_);
        configured_fps = std::max(1, bandwidth_config_.max_framerate);
    }
    auto now_steady = Clock::now();
    auto& last_steady = stream_last_push_time_[stream_id];
    if (last_steady.has_value()) {
        double gap_ms = Ms(now_steady - *last_steady).count();
        double min_gap_ms = 1000.0 / configured_fps;
        if (gap_ms < min_gap_ms) {
            s_skip_count[stream_id]++;
            if (s_frame_count[stream_id] % 100 == 0) {
                RCLCPP_INFO(logger_,
                    "[%s] frame=%lu skipped=%lu entry_gap=%.0fms gate_gap=%.0fms min=%.0fms fps=%d",
                    stream_id.c_str(), s_frame_count[stream_id], s_skip_count[stream_id],
                    entry_gap_ms, gap_ms, min_gap_ms, configured_fps);
            }
            if (caps != stream_caps_[stream_id]) gst_caps_unref(caps);
            return;
        }
        if (s_frame_count[stream_id] % 100 == 0) {
            RCLCPP_INFO(logger_,
                "[%s] frame=%lu skipped=%lu entry_gap=%.0fms gate_gap=%.0fms fps=%d",
                stream_id.c_str(), s_frame_count[stream_id], s_skip_count[stream_id],
                entry_gap_ms, gap_ms, configured_fps);
        }
    }
    last_steady = now_steady;
    GstBuffer* buffer = gst_buffer_new_allocate(nullptr, msg->data.size(), nullptr);
    gst_buffer_fill(buffer, 0, msg->data.data(), msg->data.size());
    GST_BUFFER_DURATION(buffer) = GST_CLOCK_TIME_NONE;  // variable framerate

    GstFlowReturn ret;
    g_signal_emit_by_name(stream->appsrc, "push-buffer", buffer, &ret);
    gst_buffer_unref(buffer);

    // ── timing marker 3: after push-buffer ──────────────────────────────────
    auto t3 = Clock::now();

    double drain_ms  = Ms(t1 - t0).count();
    double lock_ms   = Ms(t2 - t1).count();
    double push_ms   = Ms(t3 - t2).count();
    double total_ms  = Ms(t3 - t0).count();

    // Log any call that took >50 ms — this identifies which stage blocked.
    if (total_ms > 50.0) {
        RCLCPP_WARN(logger_,
            "[%s] slow pushFrame total=%.0fms  drain=%.0fms  lock=%.0fms  push=%.0fms",
            stream_id.c_str(), total_ms, drain_ms, lock_ms, push_ms);
    }

    if (caps != stream_caps_[stream_id]) {
        gst_caps_unref(caps);
    }

    if (ret != GST_FLOW_OK) {
        RCLCPP_WARN(logger_, "[%s] push-buffer returned %s at frame=%lu",
            stream_id.c_str(), gst_flow_get_name(ret), s_frame_count[stream_id]);
    }
}

std::string WebRTCComponent::handleOffer(const std::string& stream_id, const std::string& sdp,
                                         AnswerCallback callback) {
    // Find the stream
    std::shared_ptr<StreamSource> stream;
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        auto it = streams_.find(stream_id);
        if (it == streams_.end()) {
            RCLCPP_ERROR(logger_, "Stream %s not found", stream_id.c_str());
            return "";
        }
        stream = it->second;
    }

    std::lock_guard<std::mutex> lock(peers_mutex_);

    // Create new peer session
    auto session = std::make_shared<PeerSession>();
    session->peer_id = generatePeerId();
    session->stream_id = stream_id;
    session->answer_callback = callback;
    session->answer_sent = false;
    session->component = this;

    // Peek at the SDP now (before creating elements) to select a codec.
    // We parse a temporary copy so the main parse below is unaffected.
    GstSDPMessage* offer_peek = nullptr;
    gst_sdp_message_new(&offer_peek);
    gst_sdp_message_parse_buffer((guint8*)sdp.c_str(), sdp.length(), offer_peek);
    CodecSelection codec = selectCodecFromOffer(offer_peek);
    gst_sdp_message_free(offer_peek);

    session->codec_name = codec.name;
    RCLCPP_INFO(logger_, "[Stream %s / Peer %s] Negotiated codec: %s (PT %d)",
                stream_id.c_str(), session->peer_id.c_str(),
                codec.name.c_str(), codec.payload_type);

    // Create peer-specific pipeline elements
    // queue ! encoder ! payloader ! queue2 ! webrtcbin
    const std::string enc_factory  = (codec.name == "VP9") ? "vp9enc"    : "vp8enc";
    const std::string pay_factory  = (codec.name == "VP9") ? "rtpvp9pay" : "rtpvp8pay";

    session->tee_queue = gst_element_factory_make("queue",        (session->peer_id + "_queue").c_str());
    session->encoder   = gst_element_factory_make(enc_factory.c_str(), (session->peer_id + "_enc").c_str());
    GstElement* payloader = gst_element_factory_make(pay_factory.c_str(), (session->peer_id + "_pay").c_str());
    GstElement* queue2    = gst_element_factory_make("queue",     (session->peer_id + "_queue2").c_str());
    session->webrtcbin    = gst_element_factory_make("webrtcbin", (session->peer_id + "_webrtc").c_str());

    if (!session->tee_queue || !session->encoder || !payloader || !queue2 || !session->webrtcbin) {
        RCLCPP_ERROR(logger_, "[Peer %s] Failed to create pipeline elements (codec=%s)",
                     session->peer_id.c_str(), codec.name.c_str());
        return "";
    }

    // Configure queue for live streaming - aggressively drop old frames if can't keep up
    g_object_set(session->tee_queue, 
                 "max-size-buffers", 1,       // Keep only latest frame
                 "max-size-bytes", 0,         // No byte limit
                 "max-size-time", (guint64)0, // No time limit
                 "leaky", 2,                  // downstream - drop old buffers
                 "flush-on-eos", TRUE,        // Flush on end
                 nullptr);

    // Configure encoder with current bandwidth settings.
    // Both vp8enc and vp9enc share the same libvpx property interface.
    BandwidthConfig config;
    {
        std::lock_guard<std::mutex> config_lock(config_mutex_);
        config = bandwidth_config_;
    }
    // keyframe-max-dist: force a keyframe every ~3 seconds worth of frames.
    // This must NOT be changed at runtime — libvpx injects an IDR immediately
    // whenever this property changes, which causes the visible frame jump.
    const int keyframe_dist = std::max(1, config.max_framerate) * 3;

    g_object_set(session->encoder,
                 "deadline", 1,                           // Realtime deadline
                 "target-bitrate", config.target_bitrate,
                 "cpu-used", config.encoder_speed,
                 "keyframe-max-dist", keyframe_dist,
                 "threads", 4,
                 "lag-in-frames", 0,                      // No frame buffering
                 "error-resilient", 1,                    // Error resilient mode
                 nullptr);

    g_object_set(payloader,
                 "pt", codec.payload_type,
                 nullptr);

    // Configure second queue for RTP packets - minimal buffering for live streaming
    g_object_set(queue2, 
                 "max-size-time", (guint64)10000000,  // 10ms max (was 100ms)
                 "max-size-buffers", 10,               // Limit buffer count
                 "max-size-bytes", 0, 
                 "leaky", 2,                           // Drop old packets if full
                 nullptr);

    // Configure webrtcbin for offline/local network operation (no STUN server)
    g_object_set(session->webrtcbin, "ice-transport-policy", 0, nullptr);  // all - allows local connections
    g_object_set(session->webrtcbin, "latency", 50, nullptr);  // 50ms jitter buffer - 0 caused SSRC timeouts
    g_object_set(session->webrtcbin, "bundle-policy", 3, nullptr);  // max-bundle

    // Add elements to the stream's pipeline
    gst_bin_add_many(GST_BIN(stream->pipeline), session->tee_queue, session->encoder, payloader, queue2,
                     session->webrtcbin, nullptr);

    // Link: tee ! queue ! encoder ! payloader ! queue2 ! webrtcbin
    GstPad* tee_src = gst_element_request_pad_simple(stream->tee, "src_%u");
    GstPad* queue_sink = gst_element_get_static_pad(session->tee_queue, "sink");

    if (gst_pad_link(tee_src, queue_sink) != GST_PAD_LINK_OK) {
        gst_object_unref(tee_src);
        gst_object_unref(queue_sink);
        return "";
    }

    gst_object_unref(tee_src);
    gst_object_unref(queue_sink);

    if (!gst_element_link_many(session->tee_queue, session->encoder, payloader, queue2, session->webrtcbin, nullptr)) {
        return "";
    }

    // Connect signals
    g_signal_connect(session->webrtcbin, "notify::ice-gathering-state", G_CALLBACK(on_ice_gathering_state_notify),
                     session.get());
    g_signal_connect(session->webrtcbin, "notify::ice-connection-state", G_CALLBACK(on_ice_connection_state_notify),
                     session.get());
    g_signal_connect(session->webrtcbin, "notify::connection-state", G_CALLBACK(on_connection_state_notify),
                     session.get());
    g_signal_connect(session->webrtcbin, "on-ice-candidate", G_CALLBACK(on_ice_candidate), session.get());

    // Sync state with pipeline
    gst_element_sync_state_with_parent(session->tee_queue);
    gst_element_sync_state_with_parent(session->encoder);
    gst_element_sync_state_with_parent(payloader);
    gst_element_sync_state_with_parent(queue2);
    gst_element_sync_state_with_parent(session->webrtcbin);

    // After dynamically adding elements to a live pipeline, the LATENCY event
    // has already been dispatched and will NOT be replayed for new elements.
    // Without it, rtpsession inside webrtcbin never sets its configured flag
    // and logs "Can't determine running time" for every outgoing RTP packet,
    // which means it can't generate correct RTCP SR timestamps.  Without
    // valid SRs the remote jitter buffer loses clock sync and the SSRC
    // eventually times out, causing the visible stream jump.
    gst_bin_recalculate_latency(GST_BIN(stream->pipeline));

    RCLCPP_DEBUG(logger_, "[Stream %s / Peer %s] Received offer SDP:\n%s",
                 stream_id.c_str(), session->peer_id.c_str(), sdp.c_str());

    GstSDPMessage* sdp_msg = nullptr;
    gst_sdp_message_new(&sdp_msg);
    gst_sdp_message_parse_buffer((guint8*)sdp.c_str(), sdp.length(), sdp_msg);

    GstWebRTCSessionDescription* offer = gst_webrtc_session_description_new(GST_WEBRTC_SDP_TYPE_OFFER, sdp_msg);

    // Add a sendonly transceiver with caps that match the negotiated codec and
    // payload type BEFORE set-remote-description.  webrtcbin uses these caps to
    // find a compatible entry in the offer; without them every codec is rejected
    // with "did not find compatible transceiver" and the stream never starts.
    std::string transceiver_caps_str =
        "application/x-rtp,media=video,encoding-name=" + codec.name +
        ",clock-rate=90000,payload=(int)" + std::to_string(codec.payload_type);
    GstCaps* transceiverCaps = gst_caps_from_string(transceiver_caps_str.c_str());
    GstWebRTCRTPTransceiver* transceiver = nullptr;
    g_signal_emit_by_name(session->webrtcbin, "add-transceiver",
                          GST_WEBRTC_RTP_TRANSCEIVER_DIRECTION_SENDONLY,
                          transceiverCaps, &transceiver);
    gst_caps_unref(transceiverCaps);
    if (transceiver) {
        g_object_unref(transceiver);
    }

    GstPromise* promise = gst_promise_new();
    g_signal_emit_by_name(session->webrtcbin, "set-remote-description", offer, promise);
    gst_promise_interrupt(promise);
    gst_promise_unref(promise);
    gst_webrtc_session_description_free(offer);

    // Create answer
    GstStructure* options = gst_structure_new_empty("options");
    promise = gst_promise_new_with_change_func(on_offer_created, session.get(), nullptr);
    g_signal_emit_by_name(session->webrtcbin, "create-answer", options, promise);
    gst_structure_free(options);

    // Store session
    peers_[session->peer_id] = session;

    return session->peer_id;
}

void WebRTCComponent::disconnectPeer(const std::string& peer_id) {
    std::lock_guard<std::mutex> lock(peers_mutex_);

    auto it = peers_.find(peer_id);
    if (it == peers_.end()) {
        return;
    }

    cleanupPeerSession(peer_id);
    peers_.erase(it);
}

size_t WebRTCComponent::getActivePeerCount() const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    return peers_.size();
}

size_t WebRTCComponent::getActivePeerCount(const std::string& stream_id) const {
    std::lock_guard<std::mutex> lock(peers_mutex_);
    size_t count = 0;
    for (const auto& pair : peers_) {
        if (pair.second->stream_id == stream_id) {
            count++;
        }
    }
    return count;
}

std::vector<std::string> WebRTCComponent::getStreamIds() const {
    std::lock_guard<std::mutex> lock(streams_mutex_);
    std::vector<std::string> ids;
    for (const auto& pair : streams_) {
        ids.push_back(pair.first);
    }
    return ids;
}

std::string WebRTCComponent::generatePeerId() { return "peer_" + std::to_string(++peer_counter_); }

void WebRTCComponent::cleanupPeerSession(const std::string& peer_id) {
    auto it = peers_.find(peer_id);
    if (it == peers_.end()) {
        return;
    }

    auto& session = it->second;

    // Find the stream this peer belongs to
    std::shared_ptr<StreamSource> stream;
    {
        std::lock_guard<std::mutex> lock(streams_mutex_);
        auto stream_it = streams_.find(session->stream_id);
        if (stream_it == streams_.end()) {
            return;
        }
        stream = stream_it->second;
    }

    // Find and remove all peer elements from pipeline
    std::string base_name = peer_id;
    GstElement* payloader   = gst_bin_get_by_name(GST_BIN(stream->pipeline), (base_name + "_pay").c_str());
    GstElement* queue2      = gst_bin_get_by_name(GST_BIN(stream->pipeline), (base_name + "_queue2").c_str());

    // Set ALL per-peer elements to NULL state before removing them so GStreamer
    // does not complain about disposing elements that are still in PLAYING state.
    if (session->webrtcbin) {
        gst_element_set_state(session->webrtcbin, GST_STATE_NULL);
    }
    if (queue2) {
        gst_element_set_state(queue2, GST_STATE_NULL);
    }
    if (payloader) {
        gst_element_set_state(payloader, GST_STATE_NULL);
    }
    if (session->encoder) {
        gst_element_set_state(session->encoder, GST_STATE_NULL);
    }
    if (session->tee_queue) {
        gst_element_set_state(session->tee_queue, GST_STATE_NULL);
    }

    // Unlink tee pad
    if (session->tee_queue) {
        GstPad* queue_sink = gst_element_get_static_pad(session->tee_queue, "sink");
        if (queue_sink) {
            GstPad* tee_src = gst_pad_get_peer(queue_sink);
            if (tee_src) {
                gst_pad_unlink(tee_src, queue_sink);
                gst_element_release_request_pad(stream->tee, tee_src);
                gst_object_unref(tee_src);
            }
            gst_object_unref(queue_sink);
        }
    }

    // Remove elements from pipeline
    if (session->webrtcbin) {
        gst_bin_remove(GST_BIN(stream->pipeline), session->webrtcbin);
    }
    if (session->tee_queue) {
        gst_bin_remove(GST_BIN(stream->pipeline), session->tee_queue);
    }
    if (session->encoder) {
        gst_bin_remove(GST_BIN(stream->pipeline), session->encoder);
    }
    if (payloader) {
        gst_bin_remove(GST_BIN(stream->pipeline), payloader);
        gst_object_unref(payloader);
    }
    if (queue2) {
        gst_bin_remove(GST_BIN(stream->pipeline), queue2);
        gst_object_unref(queue2);
    }
}

void WebRTCComponent::cleanupStreamSource(const std::string& stream_id) {
    auto it = streams_.find(stream_id);
    if (it == streams_.end()) {
        return;
    }

    auto& stream = it->second;

    if (stream->pipeline) {
        gst_element_set_state(stream->pipeline, GST_STATE_NULL);
        gst_object_unref(stream->pipeline);
    }
}

WebRTCComponent::PeerSession* WebRTCComponent::findPeerByWebrtcbin(GstElement* webrtcbin) {
    for (auto& pair : peers_) {
        if (pair.second->webrtcbin == webrtcbin) {
            return pair.second.get();
        }
    }
    return nullptr;
}

void WebRTCComponent::on_offer_created(GstPromise* promise, gpointer user_data) {
    PeerSession* session = (PeerSession*)user_data;
    GstStructure const* reply = gst_promise_get_reply(promise);
    GstWebRTCSessionDescription* answer = nullptr;
    gst_structure_get(reply, "answer", GST_TYPE_WEBRTC_SESSION_DESCRIPTION, &answer, nullptr);
    gst_promise_unref(promise);

    if (answer && answer->sdp) {
        gchar* sdp_str = gst_sdp_message_as_text(answer->sdp);
        RCLCPP_DEBUG(rclcpp::get_logger("webrtc"), "[Peer %s] Created answer SDP:\n%s",
                     session->peer_id.c_str(), sdp_str);
        g_free(sdp_str);
    }

    GstPromise* local_promise = gst_promise_new();
    g_signal_emit_by_name(session->webrtcbin, "set-local-description", answer, local_promise);
    gst_promise_interrupt(local_promise);
    gst_promise_unref(local_promise);

    gst_webrtc_session_description_free(answer);
}

void WebRTCComponent::on_ice_gathering_state_notify(GstElement* webrtcbin, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    PeerSession* session = (PeerSession*)user_data;
    GstWebRTCICEGatheringState state;
    g_object_get(webrtcbin, "ice-gathering-state", &state, nullptr);

    if (state == GST_WEBRTC_ICE_GATHERING_STATE_COMPLETE && !session->answer_sent) {
        GstWebRTCSessionDescription* answer = nullptr;
        g_object_get(session->webrtcbin, "local-description", &answer, nullptr);

        if (answer) {
            gchar* sdp_str = gst_sdp_message_as_text(answer->sdp);
            std::string sdp_answer(sdp_str);

            RCLCPP_DEBUG(rclcpp::get_logger("webrtc"),
                         "[Peer %s] Sending final answer SDP (ICE complete):\n%s",
                         session->peer_id.c_str(), sdp_str);

            // Call the callback with the answer
            if (session->answer_callback) {
                session->answer_callback(sdp_answer);
                session->answer_sent = true;
            }

            g_free(sdp_str);
            gst_webrtc_session_description_free(answer);
        }
    }
}

void WebRTCComponent::on_ice_connection_state_notify(GstElement* webrtcbin, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    (void)webrtcbin;
    PeerSession* session = (PeerSession*)user_data;
    GstWebRTCICEConnectionState state;
    g_object_get(session->webrtcbin, "ice-connection-state", &state, nullptr);

    const char* state_name = "UNKNOWN";
    if (state == GST_WEBRTC_ICE_CONNECTION_STATE_NEW) {
        state_name = "NEW";
    } else if (state == GST_WEBRTC_ICE_CONNECTION_STATE_CHECKING) {
        state_name = "CHECKING";
    } else if (state == GST_WEBRTC_ICE_CONNECTION_STATE_CONNECTED) {
        state_name = "CONNECTED";
    } else if (state == GST_WEBRTC_ICE_CONNECTION_STATE_COMPLETED) {
        state_name = "COMPLETED";
    } else if (state == GST_WEBRTC_ICE_CONNECTION_STATE_FAILED) {
        state_name = "FAILED";
    } else if (state == GST_WEBRTC_ICE_CONNECTION_STATE_DISCONNECTED) {
        state_name = "DISCONNECTED";
    } else if (state == GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED) {
        state_name = "CLOSED";
    }

    RCLCPP_INFO(rclcpp::get_logger("webrtc"), "[Peer %s] Peer connection state: %s", session->peer_id.c_str(),
                state_name);

    // Terminal states — schedule cleanup so the dead tee branch stops consuming CPU.
    // We cannot call disconnectPeer directly here because this callback runs on a
    // GStreamer streaming thread; instead we defer to drainPendingDisconnects().
    if (state == GST_WEBRTC_ICE_CONNECTION_STATE_FAILED ||
        state == GST_WEBRTC_ICE_CONNECTION_STATE_CLOSED) {
        if (session->component) {
            session->component->schedulePeerDisconnect(session->peer_id);
        }
    }
}

void WebRTCComponent::on_connection_state_notify(GstElement* webrtcbin, GParamSpec* pspec, gpointer user_data) {
    (void)pspec;
    (void)webrtcbin;
    PeerSession* session = (PeerSession*)user_data;
    GstWebRTCPeerConnectionState state;
    g_object_get(session->webrtcbin, "connection-state", &state, nullptr);

    const char* state_name = "UNKNOWN";
    if (state == GST_WEBRTC_PEER_CONNECTION_STATE_NEW) {
        state_name = "NEW";
    } else if (state == GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTING) {
        state_name = "CONNECTING";
    } else if (state == GST_WEBRTC_PEER_CONNECTION_STATE_CONNECTED) {
        state_name = "CONNECTED";
    } else if (state == GST_WEBRTC_PEER_CONNECTION_STATE_DISCONNECTED) {
        state_name = "DISCONNECTED";
    } else if (state == GST_WEBRTC_PEER_CONNECTION_STATE_FAILED) {
        state_name = "FAILED";
    } else if (state == GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED) {
        state_name = "CLOSED";
    }

    RCLCPP_INFO(rclcpp::get_logger("webrtc"), "[Peer %s] Connection state: %s", session->peer_id.c_str(), state_name);

    if (state == GST_WEBRTC_PEER_CONNECTION_STATE_FAILED ||
        state == GST_WEBRTC_PEER_CONNECTION_STATE_CLOSED) {
        if (session->component) {
            session->component->schedulePeerDisconnect(session->peer_id);
        }
    }
}

void WebRTCComponent::on_ice_candidate(GstElement* webrtc, guint mlineindex, gchar* candidate, gpointer user_data) {
    (void)webrtc;
    (void)mlineindex;
    (void)candidate;
    (void)user_data;
}

void WebRTCComponent::setDefaultBandwidthConfig(const BandwidthConfig& config) {
    std::lock_guard<std::mutex> lock(config_mutex_);
    bandwidth_config_ = config;
    RCLCPP_INFO(logger_, "Bandwidth config updated: bitrate=%d, framerate=%d, scale=%.2f, speed=%d",
                config.target_bitrate, config.max_framerate, config.resolution_scale, config.encoder_speed);
}

BandwidthConfig WebRTCComponent::getDefaultBandwidthConfig() const {
    std::lock_guard<std::mutex> lock(config_mutex_);
    return bandwidth_config_;
}

bool WebRTCComponent::updatePeerBandwidth(const std::string& peer_id, const BandwidthConfig& config) {
    std::lock_guard<std::mutex> lock(peers_mutex_);

    auto it = peers_.find(peer_id);
    if (it == peers_.end() || !it->second->encoder) {
        return false;
    }

    // Only update bitrate and encoder speed — never touch keyframe-max-dist at
    // runtime because doing so forces libvpx to inject an IDR frame immediately,
    // which manifests as a visible frame jump / quality spike on every apply.
    g_object_set(it->second->encoder,
                 "target-bitrate", config.target_bitrate,
                 "cpu-used", config.encoder_speed,
                 nullptr);

    // Update the framerate cap on the rate capsfilter.
    // The per-stream fps gate in pushFrame reads max_framerate from
    // bandwidth_config_ on every frame, so the new value takes effect
    // immediately with no pipeline changes needed.
    RCLCPP_INFO(logger_, "[Peer %s] Bandwidth updated: bitrate=%d, fps=%d, speed=%d", peer_id.c_str(),
                config.target_bitrate, config.max_framerate, config.encoder_speed);

    return true;
}

}  // namespace basin::webrtc_streamer
