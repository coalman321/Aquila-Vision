#include "basin/node_core/node_params.hpp"

namespace basin::node_core {

NodeParams::NodeParams(rclcpp::Node* node) : node_(node) {
    update_handler_ = std::make_shared<rclcpp::ParameterEventHandler>(node);
}

}  // namespace basin::node_core
