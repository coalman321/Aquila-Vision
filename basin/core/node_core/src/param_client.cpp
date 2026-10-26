#include "basin/node_core/param_client.hpp"

namespace basin::node_core {

ParamClient::ParamClient(rclcpp::Node::SharedPtr node, int64_t timeout_ms) :
    node_(node), timeout_(timeout_ms), last_error_("") {}

rclcpp::Client<rcl_interfaces::srv::SetParameters>::SharedPtr ParamClient::getClient(
    const std::string& target_node_name) {
    // Check if we already have a client for this node
    auto it = clients_.find(target_node_name);
    if (it != clients_.end()) {
        return it->second;
    }

    // Create a new client for this node
    std::string service_name = target_node_name + "/set_parameters";
    auto client = node_->create_client<rcl_interfaces::srv::SetParameters>(service_name);

    // Cache it for future use
    clients_[target_node_name] = client;

    return client;
}

bool ParamClient::isNodeAvailable(const std::string& target_node_name, int64_t timeout_ms) {
    last_error_.clear();

    if (target_node_name.empty()) {
        last_error_ = "Target node name cannot be empty";
        return false;
    }

    // Use provided timeout or default
    auto check_timeout = (timeout_ms > 0) ? std::chrono::milliseconds(timeout_ms) : timeout_;

    // Get or create the service client
    auto client = getClient(target_node_name);

    // Check if the service is available
    bool available = client->wait_for_service(check_timeout);

    if (!available) {
        last_error_ = "Service " + target_node_name + "/set_parameters not available";
        RCLCPP_DEBUG(node_->get_logger(), "%s", last_error_.c_str());
    } else {
        RCLCPP_DEBUG(node_->get_logger(), "Node %s is available", target_node_name.c_str());
    }

    return available;
}

bool ParamClient::setParameter(const std::string& target_node_name, const rclcpp::Parameter& param) {
    return setParameters(target_node_name, { param });
}

bool ParamClient::setParameters(const std::string& target_node_name, const std::vector<rclcpp::Parameter>& params) {
    last_error_.clear();

    if (target_node_name.empty()) {
        last_error_ = "Target node name cannot be empty";
        RCLCPP_ERROR(node_->get_logger(), "%s", last_error_.c_str());
        return false;
    }

    if (params.empty()) {
        last_error_ = "Parameter list cannot be empty";
        RCLCPP_ERROR(node_->get_logger(), "%s", last_error_.c_str());
        return false;
    }

    // Get or create the service client
    auto client = getClient(target_node_name);

    // Wait for the service to be available
    if (!client->wait_for_service(timeout_)) {
        last_error_ = "Service " + target_node_name + "/set_parameters not available after waiting";
        RCLCPP_ERROR(node_->get_logger(), "%s", last_error_.c_str());
        return false;
    }

    // Create the request
    auto request = std::make_shared<rcl_interfaces::srv::SetParameters::Request>();
    for (const auto& param : params) {
        request->parameters.push_back(param.to_parameter_msg());
    }

    RCLCPP_DEBUG(node_->get_logger(), "Setting %zu parameter(s) on node: %s", params.size(), target_node_name.c_str());

    // Make the synchronous call
    auto future = client->async_send_request(request);

    // Wait for the response (blocking, without spinning the node)
    // Poll the future with small intervals to allow external executor to process response
    auto start_time = std::chrono::steady_clock::now();
    auto poll_interval = std::chrono::milliseconds(10);

    while (std::chrono::steady_clock::now() - start_time < timeout_) {
        auto future_status = future.wait_for(poll_interval);
        if (future_status == std::future_status::ready) {
            break;
        }
    }

    // Check if future is ready
    if (future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
        last_error_ = "Service call timed out";
        RCLCPP_ERROR(node_->get_logger(), "%s", last_error_.c_str());
        return false;
    }

    // Get the response
    auto response = future.get();

    // Check results - all parameters must be successfully set
    bool all_success = true;
    for (size_t i = 0; i < response->results.size(); ++i) {
        const auto& result = response->results[i];
        if (!result.successful) {
            all_success = false;
            std::string param_name = i < params.size() ? params[i].get_name() : "unknown";
            last_error_ += "Failed to set parameter '" + param_name + "': " + result.reason + "; ";
            RCLCPP_ERROR(node_->get_logger(), "Failed to set parameter '%s': %s", param_name.c_str(),
                         result.reason.c_str());
        } else {
            std::string param_name = i < params.size() ? params[i].get_name() : "unknown";
            RCLCPP_DEBUG(node_->get_logger(), "Successfully set parameter '%s'", param_name.c_str());
        }
    }

    if (!all_success && last_error_.empty()) {
        last_error_ = "Some parameters failed to set";
    }

    return all_success;
}

}  // namespace basin::node_core
