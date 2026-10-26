#include <memory>
#include <rclcpp/rclcpp.hpp>

#include "camera_cal/camera_cal_node.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);

    auto node = std::make_shared<camera_cal::CameraCalNode>(rclcpp::NodeOptions());
    rclcpp::spin(node);

    rclcpp::shutdown();
}
