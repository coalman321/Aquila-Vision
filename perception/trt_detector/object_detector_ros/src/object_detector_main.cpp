#include <memory>
#include <object_detector_ros/object_detector_node.hpp>
#include <rclcpp/rclcpp.hpp>

int main(int argc, char **argv) {
    rclcpp::init(argc, argv);

    auto node = std::make_shared<object_detector::ObjectDetectorNode>();
    rclcpp::spin(node);

    rclcpp::shutdown();
}
