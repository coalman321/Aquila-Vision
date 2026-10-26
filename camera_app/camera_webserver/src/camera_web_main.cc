#include <memory>
#include <rclcpp/rclcpp.hpp>

#include "camera_webserver/camera_web_node.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);

    auto node = std::make_shared<camera_web::CameraWebNode>();
    rclcpp::spin(node);

    rclcpp::shutdown();
}
