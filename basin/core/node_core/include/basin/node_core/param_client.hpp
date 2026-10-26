#pragma once

#include <chrono>
#include <memory>
#include <rcl_interfaces/srv/set_parameters.hpp>
#include <rclcpp/parameter.hpp>
#include <rclcpp/rclcpp.hpp>
#include <string>

namespace basin::node_core {

/**
 * @brief A blocking ROS2 parameter setting client for updating parameters on remote nodes
 *
 * This class provides a simple API to set parameters on any ROS2 node in the system.
 * All operations are blocking/synchronous.
 */
class ParamClient {
   public:
    /**
     * @brief Construct a new ParamClient
     *
     * @param node The ROS2 node that will own the service client
     * @param timeout_ms Timeout for service calls in milliseconds (default: 5000ms)
     */
    explicit ParamClient(rclcpp::Node::SharedPtr node, int64_t timeout_ms = 5000);

    /**
     * @brief Check if a node is online and its parameter service is available
     *
     * This method can be called before setParameter/setParameters to verify
     * that the target node is running and ready to accept parameter changes.
     *
     * @param target_node_name Fully qualified name of the node to check (e.g., "/my_node")
     * @param timeout_ms Optional timeout in milliseconds (uses default if not specified)
     * @return true if the node's parameter service is available
     * @return false if the service is not available
     */
    bool isNodeAvailable(const std::string& target_node_name, int64_t timeout_ms = -1);

    /**
     * @brief Set a parameter on a target node
     *
     * @param target_node_name Fully qualified name of the node to update (e.g., "/my_node")
     * @param param The parameter to set (contains name and value)
     * @return true if the parameter was successfully set
     * @return false if the operation failed
     */
    bool setParameter(const std::string& target_node_name, const rclcpp::Parameter& param);

    /**
     * @brief Set multiple parameters on a target node
     *
     * @param target_node_name Fully qualified name of the node to update (e.g., "/my_node")
     * @param params Vector of parameters to set
     * @return true if all parameters were successfully set
     * @return false if any operation failed
     */
    bool setParameters(const std::string& target_node_name, const std::vector<rclcpp::Parameter>& params);

    /**
     * @brief Get the last error message
     *
     * @return std::string Description of the last error
     */
    std::string getLastError() const { return last_error_; }

   private:
    rclcpp::Node::SharedPtr node_;
    std::chrono::milliseconds timeout_;
    std::string last_error_;

    /**
     * @brief Get or create a service client for the target node
     *
     * @param target_node_name The node to get the client for
     * @return Service client for setting parameters
     */
    rclcpp::Client<rcl_interfaces::srv::SetParameters>::SharedPtr getClient(const std::string& target_node_name);

    // Cache of service clients to avoid recreating them
    std::map<std::string, rclcpp::Client<rcl_interfaces::srv::SetParameters>::SharedPtr> clients_;
};

}  // namespace basin::node_core
