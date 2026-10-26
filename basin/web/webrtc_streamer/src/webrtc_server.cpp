#include "basin/webrtc_streamer/webrtc_server.hpp"

#include <algorithm>
#include <nlohmann/json.hpp>

#include "basin/webrtc_streamer/webrtc_component.hpp"

using json = nlohmann::json;

namespace basin::webrtc_streamer {

WebRTCServer::WebRTCServer(basin::web_core::WebNode* node, std::shared_ptr<WebRTCComponent> webrtc_component,
                           StreamCreationCallback stream_creator, AvailableTopicsCallback available_topics_getter) :
    node_(node), webrtc_component_(webrtc_component), stream_creator_(stream_creator),
    available_topics_getter_(available_topics_getter) {
    registerEndpoints(node_->getServer());
}

void WebRTCServer::registerEndpoints(httplib::Server& server) {
    // Register offer endpoint
    server.Post("/webrtc/offer", [this](const httplib::Request& req, httplib::Response& res) {
        handleOffer(req, res);
    });

    // Handle preflight OPTIONS request for CORS
    server.Options("/webrtc/offer", [this](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // Register disconnect endpoint
    server.Post("/webrtc/disconnect", [this](const httplib::Request& req, httplib::Response& res) {
        handleDisconnect(req, res);
    });

    server.Options("/webrtc/disconnect", [this](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // Register status endpoint
    server.Get("/webrtc/status", [this](const httplib::Request& req, httplib::Response& res) {
        handleStatus(req, res);
    });

    server.Options("/webrtc/status", [this](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // Register bandwidth configuration endpoints
    server.Get("/webrtc/bandwidth", [this](const httplib::Request& req, httplib::Response& res) {
        handleGetBandwidthConfig(req, res);
    });

    server.Options("/webrtc/bandwidth", [this](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    server.Post("/webrtc/bandwidth", [this](const httplib::Request& req, httplib::Response& res) {
        handleSetBandwidthConfig(req, res);
    });

    server.Post("/webrtc/bandwidth/peer", [this](const httplib::Request& req, httplib::Response& res) {
        handleUpdatePeerBandwidth(req, res);
    });

    server.Options("/webrtc/bandwidth/peer", [this](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    // Register stream management endpoints
    server.Get("/webrtc/streams", [this](const httplib::Request& req, httplib::Response& res) {
        handleListStreams(req, res);
    });

    server.Options("/webrtc/streams", [this](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });

    server.Get("/webrtc/streams/:stream_id", [this](const httplib::Request& req, httplib::Response& res) {
        handleStreamStatus(req, res);
    });

    server.Options("/webrtc/streams/:stream_id", [this](const httplib::Request&, httplib::Response& res) {
        res.status = 204;
    });
}

void WebRTCServer::handleOffer(const httplib::Request& req, httplib::Response& res) {
    try {
        // Parse the offer
        auto j = json::parse(req.body);
        std::string sdp = j["sdp"];
        std::string type = j["type"];

        // Get stream_id (default to "default" for backward compatibility)
        std::string stream_id = j.value("stream_id", "default");

        // Create stream on-demand if it doesn't exist
        if (!stream_creator_(stream_id)) {
            res.status = 404;
            res.set_content("Stream not found or failed to create", "text/plain");
            return;
        }

        // Create a promise to wait for the answer
        std::promise<std::string> answer_promise;
        auto answer_future = answer_promise.get_future();

        // Handle the offer with a callback (returns peer_id)
        std::string peer_id = webrtc_component_->handleOffer(
            stream_id, sdp, [&answer_promise](const std::string& answer_sdp) { answer_promise.set_value(answer_sdp); });

        if (peer_id.empty()) {
            res.status = 500;
            res.set_content("Failed to create peer connection or stream not found", "text/plain");
            return;
        }

        // Wait for the answer (with timeout)
        if (answer_future.wait_for(std::chrono::seconds(5)) == std::future_status::ready) {
            std::string answer_sdp = answer_future.get();

            json response;
            response["type"] = "answer";
            response["sdp"] = answer_sdp;
            response["peer_id"] = peer_id;
            response["stream_id"] = stream_id;
            res.set_content(response.dump(), "application/json");
        } else {
            res.status = 500;
            res.set_content("Timeout generating answer", "text/plain");
            // Clean up the peer on timeout
            webrtc_component_->disconnectPeer(peer_id);
        }

    } catch (const std::exception& e) {
        (void)e;
        res.status = 400;
        res.set_content("Bad request", "text/plain");
    }
}

void WebRTCServer::handleDisconnect(const httplib::Request& req, httplib::Response& res) {
    try {
        auto j = json::parse(req.body);
        std::string peer_id = j["peer_id"];

        webrtc_component_->disconnectPeer(peer_id);

        res.status = 200;
        res.set_content("Disconnected", "text/plain");
    } catch (const std::exception& e) {
        (void)e;
        res.status = 400;
        res.set_content("Bad request", "text/plain");
    }
}

void WebRTCServer::handleStatus(const httplib::Request&, httplib::Response& res) {
    json response;
    response["active_peers"] = webrtc_component_->getActivePeerCount();
    res.set_content(response.dump(), "application/json");
}

void WebRTCServer::handleGetBandwidthConfig(const httplib::Request&, httplib::Response& res) {
    BandwidthConfig config = webrtc_component_->getDefaultBandwidthConfig();

    json response;
    response["target_bitrate"] = config.target_bitrate;
    response["max_framerate"] = config.max_framerate;
    response["resolution_scale"] = config.resolution_scale;
    response["encoder_speed"] = config.encoder_speed;

    res.set_content(response.dump(), "application/json");
}

void WebRTCServer::handleSetBandwidthConfig(const httplib::Request& req, httplib::Response& res) {
    try {
        auto j = json::parse(req.body);

        BandwidthConfig config = webrtc_component_->getDefaultBandwidthConfig();

        // Update only provided fields
        if (j.contains("target_bitrate")) {
            config.target_bitrate = j["target_bitrate"];
        }
        if (j.contains("max_framerate")) {
            config.max_framerate = j["max_framerate"];
        }
        if (j.contains("resolution_scale")) {
            config.resolution_scale = j["resolution_scale"];
        }
        if (j.contains("encoder_speed")) {
            config.encoder_speed = j["encoder_speed"];
        }

        webrtc_component_->setDefaultBandwidthConfig(config);

        json response;
        response["status"] = "success";
        response["config"] = { { "target_bitrate", config.target_bitrate },
                               { "max_framerate", config.max_framerate },
                               { "resolution_scale", config.resolution_scale },
                               { "encoder_speed", config.encoder_speed } };
        res.set_content(response.dump(), "application/json");

    } catch (const std::exception& e) {
        (void)e;
        res.status = 400;
        res.set_content("Bad request", "text/plain");
    }
}

void WebRTCServer::handleUpdatePeerBandwidth(const httplib::Request& req, httplib::Response& res) {
    try {
        auto j = json::parse(req.body);
        std::string peer_id = j["peer_id"];

        BandwidthConfig config = webrtc_component_->getDefaultBandwidthConfig();

        // Update only provided fields
        if (j.contains("target_bitrate")) {
            config.target_bitrate = j["target_bitrate"];
        }
        if (j.contains("max_framerate")) {
            config.max_framerate = j["max_framerate"];
        }
        if (j.contains("resolution_scale")) {
            config.resolution_scale = j["resolution_scale"];
        }
        if (j.contains("encoder_speed")) {
            config.encoder_speed = j["encoder_speed"];
        }

        bool success = webrtc_component_->updatePeerBandwidth(peer_id, config);

        if (success) {
            json response;
            response["status"] = "success";
            response["peer_id"] = peer_id;
            res.set_content(response.dump(), "application/json");
        } else {
            res.status = 404;
            res.set_content("Peer not found", "text/plain");
        }

    } catch (const std::exception& e) {
        (void)e;
        res.status = 400;
        res.set_content("Bad request", "text/plain");
    }
}

void WebRTCServer::handleListStreams(const httplib::Request&, httplib::Response& res) {
    // Get all available topics (discovered but not necessarily active)
    std::vector<std::string> available_topics = available_topics_getter_();
    
    // Get currently registered streams from the WebRTC component
    std::vector<std::string> registered_streams = webrtc_component_->getStreamIds();

    json response;
    response["streams"] = json::array();

    for (const auto& topic_name : available_topics) {
        json stream_info;
        stream_info["stream_id"] = topic_name;
        
        // Check if this stream is currently active (registered)
        bool is_active = std::find(registered_streams.begin(), registered_streams.end(), topic_name) != registered_streams.end();
        stream_info["is_active"] = is_active;
        
        // Only report active peer count if stream is active
        if (is_active) {
            stream_info["active_peers"] = webrtc_component_->getActivePeerCount(topic_name);
        } else {
            stream_info["active_peers"] = 0;
        }
        
        response["streams"].push_back(stream_info);
    }

    res.set_content(response.dump(), "application/json");
}

void WebRTCServer::handleStreamStatus(const httplib::Request& req, httplib::Response& res) {
    std::string stream_id = req.path_params.at("stream_id");

    // Check if stream exists in available topics
    auto available_topics = available_topics_getter_();
    bool stream_available = std::find(available_topics.begin(), available_topics.end(), stream_id) != available_topics.end();

    if (!stream_available) {
        res.status = 404;
        res.set_content("Stream not found", "text/plain");
        return;
    }

    // Check if stream is currently active (registered)
    auto registered_streams = webrtc_component_->getStreamIds();
    bool is_active = std::find(registered_streams.begin(), registered_streams.end(), stream_id) != registered_streams.end();

    json response;
    response["stream_id"] = stream_id;
    response["is_active"] = is_active;
    
    if (is_active) {
        response["active_peers"] = webrtc_component_->getActivePeerCount(stream_id);
    } else {
        response["active_peers"] = 0;
    }

    res.set_content(response.dump(), "application/json");
}

}  // namespace basin::webrtc_streamer
