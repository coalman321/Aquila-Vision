#pragma once

#include <basin/web_core/web_node.hpp>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <process_msgs/msg/process_list.hpp>
#include <process_msgs/srv/remote_request.hpp>
#include <string>
#include "process_msgs/srv/remote_request.hpp"

namespace basin::web_core {

/// @brief Configuration for launch component API routes
struct LaunchComponentConfig {
    std::string process_list_route = "/api/launch/list";            ///< Route for process list endpoint
    std::string process_list_topic = "/state/launch/process_list";  ///< Topic to subscribe to
    std::string request_route = "/api/launch/request";              ///< Route for remote request endpoint
    std::string request_service_prefix = "/state/launch";           ///< Service name prefix for remote requests (full name: /state/launch/<system_name>/request)
    int queue_size = 10;                                            ///< Subscription queue size
    int service_timeout_ms = 5000;                                  ///< Service call timeout in milliseconds
    int process_list_timeout_ms = 5000;                            ///< Timeout for process list entries in milliseconds
};

/// @brief Generic component that adds launch/process management API endpoints to WebNode
///
/// This component registers itself to an existing WebNode to provide process monitoring
/// capabilities via configurable HTTP endpoints. It subscribes to process list topics
/// and serves the information via HTTP GET requests.
///
/// Usage:
///   auto web_node = std::make_shared<WebNode>();
///   LaunchComponentConfig config;
///   config.process_list_route = "/api/launch/processes";
///   auto launch_component = std::make_shared<LaunchComponent>(web_node, config);
///   web_node->startServer();
class LaunchComponent {
   public:
    /// @brief Construct and register the component to a WebNode with default config
    /// @param web_node Shared pointer to the WebNode to register with
    explicit LaunchComponent(std::shared_ptr<WebNode> web_node);

    /// @brief Construct and register the component to a WebNode with custom config
    /// @param web_node Shared pointer to the WebNode to register with
    /// @param config Configuration for the component's routes and topics
    LaunchComponent(std::shared_ptr<WebNode> web_node, const LaunchComponentConfig& config);

    ~LaunchComponent() = default;

    /// @brief List the available systems with process lists
    /// @return Vector of system names
    std::vector<std::string> listSystems();

    /// @brief Get the current process lists
    /// @return Map of system names to their process lists
    std::map<std::string, process_msgs::msg::ProcessList> getProcessLists();

    /// @brief Call a remote request service on a specified system
    /// @param system_name The system name to call the service on
    /// @param request The remote request to send
    /// @return The response from the remote request service
    process_msgs::srv::RemoteRequest::Response callRemoteRequest(
        const std::string& system_name, const process_msgs::srv::RemoteRequest::Request& request);

   private:
    /// @brief Callback for process list messages
    /// @param msg The process list message
    void processListCallback(const process_msgs::msg::ProcessList::SharedPtr msg);

    /// @brief Register the process list API endpoint to the web node
    void registerProcessListRoute();

    /// @brief Register the remote request API endpoint to the web node
    void registerRequestRoute();

    /// @brief Get or create a client for the specified system name
    /// @param system_name The system name to get/create a client for
    /// @return Shared pointer to the client for the system
    rclcpp::Client<process_msgs::srv::RemoteRequest>::SharedPtr getOrCreateClient(const std::string& system_name);

    std::shared_ptr<WebNode> web_node_;
    LaunchComponentConfig config_;
    rclcpp::Subscription<process_msgs::msg::ProcessList>::SharedPtr process_list_sub_;
    std::map<std::string, rclcpp::Client<process_msgs::srv::RemoteRequest>::SharedPtr> request_clients_;
    std::map<std::string, std::pair<process_msgs::msg::ProcessList, std::chrono::steady_clock::time_point>> process_lists_;
    std::mutex process_list_mutex_;
    std::mutex clients_mutex_;
};

}  // namespace basin::web_core
