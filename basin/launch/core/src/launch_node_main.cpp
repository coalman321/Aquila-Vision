#include <memory>
#include <rclcpp/rclcpp.hpp>

#include "launch_core/launch_node.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);

    try {
        rclcpp::NodeOptions options;
        auto node = std::make_shared<basin::launch_core::LaunchNode>(options);

        rclcpp::spin(node);

        rclcpp::shutdown();

        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        rclcpp::shutdown();
        return 1;
    }
}
