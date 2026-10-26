#include <memory>
#include <rclcpp/rclcpp.hpp>

#include "continous_logger/logger_node.hpp"

int main(int argc, char** argv) {
    // Initialize ROS 2
    rclcpp::init(argc, argv);

    try {
        // Create the logger node
        rclcpp::NodeOptions options;
        auto node = std::make_shared<basin::continuous_logger::LoggerNode>(options);

        // Spin the node
        rclcpp::spin(node);

        // Clean shutdown
        rclcpp::shutdown();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        rclcpp::shutdown();
        return 1;
    }
}
