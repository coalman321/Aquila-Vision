#include "basin/web_core/web_node.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <rclcpp/logging.hpp>
#include <sstream>

namespace basin::web_core {

WebNode::WebNode(const std::string & name, const rclcpp::NodeOptions& options) :
    basin::node_core::NodeCore(name, "", options), running_(false) {
    // Example API endpoint
    svr_.Get("/api/status", [](const httplib::Request&, httplib::Response& res) {
        // Return some status info
        res.set_content("{\"status\": \"running\"}", "application/json");
    });

    // check parameters to see if we want to enable blanket CORS
    bool enable_cors = this->declare_parameter("enable_blanket_cors", true);
    if (enable_cors) {
        RCLCPP_INFO(get_logger(), "Enabling blanket CORS for all routes");

        // make a blanket options handler for all routes
        svr_.Options(R"(/.*)", [](const httplib::Request&, httplib::Response& res) { res.status = 200; });

        // also apply CORS headers to all responses
        svr_.set_post_routing_handler([](const httplib::Request& req, httplib::Response& res) {
            // Check if request has Origin header, if so use it instead of *
            // This allows credentials to work properly
            if (req.has_header("Origin")) {
                res.set_header("Access-Control-Allow-Origin", req.get_header_value("Origin"));
                res.set_header("Access-Control-Allow-Credentials", "true");
            } else {
                res.set_header("Access-Control-Allow-Origin", "*");
            }
            res.set_header("Access-Control-Allow-Methods", "GET, POST, PUT, DELETE, OPTIONS");
            res.set_header("Access-Control-Allow-Headers", "Content-Type, Authorization");
        });
    }
}

WebNode::~WebNode() { shutdown(); }

// File upload registration is handled now via `FileManagerComponent`.

// Removed file management routes; use FileManagerComponent instead.

void WebNode::shutdown() { stopServer(); }

void WebNode::startServer() {
    if (running_) {
        return;
    }
    running_ = true;

    // Get port from parameter or default to 8080
    int port = this->declare_parameter("port", 8080);

    RCLCPP_INFO(this->get_logger(), "Starting web server on port %d", port);
    server_thread_ = std::thread([this, port]() { svr_.listen("0.0.0.0", port); });
}

void WebNode::stopServer() {
    if (!running_) {
        return;
    }
    running_ = false;
    svr_.stop();
    if (server_thread_.joinable()) {
        server_thread_.join();
    }
    RCLCPP_INFO(this->get_logger(), "Web server stopped");
}

}  // namespace basin::web_core

// #include "rclcpp_components/register_node_macro.hpp"
// RCLCPP_COMPONENTS_REGISTER_NODE(basin::web_core::WebNode)
