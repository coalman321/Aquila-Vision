#pragma once

#include <basin/web_core/web_node.hpp>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <string>

namespace basin::web_core {

/// @brief Configuration for parameter API component
struct ParameterApiComponentConfig {
    std::string base_route = "/api/parameters";  ///< Base route for parameter endpoints
    int timeout_ms = 5000;                       ///< Timeout for parameter service calls in milliseconds
};

/// @brief Component that provides REST API endpoints for ROS 2 node parameter manipulation
///
/// This component registers HTTP endpoints that allow clients to:
/// - List all parameters on a node
/// - Get specific parameter values
/// - Set parameter values
/// - Get parameter descriptions
class ParameterApiComponent {
   public:
    explicit ParameterApiComponent(std::shared_ptr<WebNode> web_node);
    ParameterApiComponent(std::shared_ptr<WebNode> web_node, const ParameterApiComponentConfig& config);
    ~ParameterApiComponent() = default;

   private:
    void registerRoutes();
    void registerListParametersRoute();
    void registerGetParameterRoute();
    void registerSetParameterRoute();
    void registerDescribeParametersRoute();

    std::shared_ptr<WebNode> web_node_;
    ParameterApiComponentConfig config_;
};

}  // namespace basin::web_core
