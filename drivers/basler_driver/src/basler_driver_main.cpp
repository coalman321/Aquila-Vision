#include <cstdio>
#include <iostream>
#include <rclcpp/rclcpp.hpp>

#include "basler_driver/basler_driver_node.hpp"

int main(int argc, char* argv[]) {
    setvbuf(stdout, nullptr, _IONBF, BUFSIZ);
    rclcpp::init(argc, argv);
    auto node = std::make_shared<basler_driver::BaslerDriverNode>();
    try {
        rclcpp::executors::SingleThreadedExecutor executor;
        executor.add_node(node);
        executor.spin();
    } catch (const std::exception& e) {
        std::cerr << "Impossible to spin: " << e.what() << std::endl;
        rclcpp::shutdown();
        return EXIT_FAILURE;
    }
    rclcpp::shutdown();
    return EXIT_SUCCESS;
}
