#include "basin/web_core/launch_component.hpp"

#include <chrono>
#include <nlohmann/json.hpp>
#include "basin/web_core/json_serialization.hpp"

using json = nlohmann::json;

namespace basin::web_core {

LaunchComponent::LaunchComponent(std::shared_ptr<WebNode> web_node) :
    LaunchComponent(web_node, LaunchComponentConfig{}) {}

LaunchComponent::LaunchComponent(std::shared_ptr<WebNode> web_node, const LaunchComponentConfig& config) :
    web_node_(web_node), config_(config) {
    if (!web_node_) {
        throw std::invalid_argument("WebNode pointer cannot be null");
    }

    registerProcessListRoute();
    registerRequestRoute();

    process_list_sub_ = web_node_->create_subscription<process_msgs::msg::ProcessList>(
        config_.process_list_topic, config_.queue_size,
        std::bind(&LaunchComponent::processListCallback, this, std::placeholders::_1));

    RCLCPP_INFO(web_node_->get_logger(), "LaunchComponent initialized with routes: %s (topic: %s), %s (service prefix: %s), timeout: %d ms",
                config_.process_list_route.c_str(), config_.process_list_topic.c_str(), config_.request_route.c_str(),
                config_.request_service_prefix.c_str(), config_.process_list_timeout_ms);
}

void LaunchComponent::registerProcessListRoute() {
    // Process List API endpoint
    web_node_->getServer().Get(config_.process_list_route, [this](const httplib::Request&, httplib::Response& res) {
        std::lock_guard<std::mutex> lock(process_list_mutex_);
        auto now = std::chrono::steady_clock::now();
        auto timeout = std::chrono::milliseconds(config_.process_list_timeout_ms);

        // Remove timed out entries
        for (auto it = process_lists_.begin(); it != process_lists_.end();) {
            if (now - it->second.second > timeout) {
                it = process_lists_.erase(it);
            } else {
                ++it;
            }
        }

        json response_json;
        for (const auto& [system_name, entry] : process_lists_) {
            const auto& process_list = entry.first;
            json system_json;
            basin::web_core::json_serialization::to_json(system_json, process_list);
            response_json[system_name] = system_json;
        }

        res.set_content(response_json.dump(), "application/json");
    });
}

void LaunchComponent::registerRequestRoute() {
    // Remote Request API endpoint (POST)
    web_node_->getServer().Post(config_.request_route, [this](const httplib::Request& req, httplib::Response& res) {
        // Parse JSON request body
        try {
            // Parse JSON body
            auto request_json = json::parse(req.body);
            
            // Extract system_name from JSON (not part of the ROS service)
            if (!request_json.contains("system_name") || !request_json["system_name"].is_string()) {
                res.status = 400;
                json error_json;
                error_json["error"] = "system_name is required in request";
                res.set_content(error_json.dump(), "application/json");
                return;
            }
            std::string system_name = request_json["system_name"];
            
            // Remove system_name from JSON before deserializing to ROS message
            request_json.erase("system_name");
            
            auto request = std::make_shared<process_msgs::srv::RemoteRequest::Request>();
            basin::web_core::json_serialization::from_json(request_json, *request);

            // Get or create client for this system
            auto client = getOrCreateClient(system_name);

            // Wait for service to be available
            if (!client->wait_for_service(std::chrono::milliseconds(1000))) {
                res.status = 503;
                json error_json;
                error_json["error"] = "Service not available for system: " + system_name;
                res.set_content(error_json.dump(), "application/json");
                return;
            }

            // Call the service
            auto future = client->async_send_request(request);

            // Wait for response with timeout
            auto status = future.wait_for(std::chrono::milliseconds(config_.service_timeout_ms));

            if (status == std::future_status::ready) {
                auto response = future.get();
                json response_json;
                basin::web_core::json_serialization::to_json(response_json, *response);
                res.set_content(response_json.dump(), "application/json");
            } else {
                res.status = 504;
                json error_json;
                error_json["error"] = "Service call timeout";
                res.set_content(error_json.dump(), "application/json");
            }
        } catch (const json::exception& e) {
            res.status = 400;
            json error_json;
            error_json["error"] = std::string("JSON parse error: ") + e.what();
            res.set_content(error_json.dump(), "application/json");
            RCLCPP_ERROR(web_node_->get_logger(), "JSON error: %s", e.what());
        } catch (const std::exception& e) {
            res.status = 400;
            json error_json;
            error_json["error"] = std::string("Invalid request: ") + e.what();
            res.set_content(error_json.dump(), "application/json");
            RCLCPP_ERROR(web_node_->get_logger(), "Error processing request: %s", e.what());
        }
    });
}

void LaunchComponent::processListCallback(const process_msgs::msg::ProcessList::SharedPtr msg) {
    std::lock_guard<std::mutex> lock(process_list_mutex_);
    process_lists_[msg->system_name] = {*msg, std::chrono::steady_clock::now()};
}

rclcpp::Client<process_msgs::srv::RemoteRequest>::SharedPtr LaunchComponent::getOrCreateClient(const std::string& system_name) {
    std::lock_guard<std::mutex> lock(clients_mutex_);
    
    auto it = request_clients_.find(system_name);
    if (it != request_clients_.end()) {
        return it->second;
    }
    
    // Create new client for this system
    std::string service_name = config_.request_service_prefix + "/" + system_name + "/request";
    auto client = web_node_->create_client<process_msgs::srv::RemoteRequest>(service_name);
    request_clients_[system_name] = client;
    
    RCLCPP_INFO(web_node_->get_logger(), "Created new client for system '%s' with service: %s", 
                system_name.c_str(), service_name.c_str());
    
    return client;
}

std::vector<std::string> LaunchComponent::listSystems() {
    std::lock_guard<std::mutex> lock(process_list_mutex_);
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(config_.process_list_timeout_ms);

    // Remove timed out entries
    for (auto it = process_lists_.begin(); it != process_lists_.end();) {
        if (now - it->second.second > timeout) {
            it = process_lists_.erase(it);
        } else {
            ++it;
        }
    }

    std::vector<std::string> systems;
    systems.reserve(process_lists_.size());
    for (const auto& kv : process_lists_) {
        systems.push_back(kv.first);
    }
    return systems;
}

std::map<std::string, process_msgs::msg::ProcessList> LaunchComponent::getProcessLists() {
    std::lock_guard<std::mutex> lock(process_list_mutex_);
    auto now = std::chrono::steady_clock::now();
    auto timeout = std::chrono::milliseconds(config_.process_list_timeout_ms);

    // Remove timed out entries
    for (auto it = process_lists_.begin(); it != process_lists_.end();) {
        if (now - it->second.second > timeout) {
            it = process_lists_.erase(it);
        } else {
            ++it;
        }
    }

    std::map<std::string, process_msgs::msg::ProcessList> results;
    for (const auto& [name, entry] : process_lists_) {
        results.emplace(name, entry.first);
    }
    return results;
}

process_msgs::srv::RemoteRequest::Response LaunchComponent::callRemoteRequest(
    const std::string& system_name, const process_msgs::srv::RemoteRequest::Request& request) {

    auto client = getOrCreateClient(system_name);

    if (!client->wait_for_service(std::chrono::milliseconds(config_.service_timeout_ms))) {
        throw std::runtime_error("Service not available for system: " + system_name);
    }

    auto req_ptr = std::make_shared<process_msgs::srv::RemoteRequest::Request>(request);
    auto future = client->async_send_request(req_ptr);
    auto status = future.wait_for(std::chrono::milliseconds(config_.service_timeout_ms));

    if (status == std::future_status::ready) {
        auto response = future.get();
        if (!response) {
            throw std::runtime_error("Received null response from service for system: " + system_name);
        }
        return *response;
    } else {
        throw std::runtime_error("Service call timeout for system: " + system_name);
    }
}

}  // namespace basin::web_core
