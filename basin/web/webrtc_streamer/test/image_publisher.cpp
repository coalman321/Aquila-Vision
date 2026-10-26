// Publishes a small moving test pattern on /debug_img (one of the topics webrtc_streamer discovers by
// default) until it is killed. Stands in for a camera in test_streamer.py.

#include <chrono>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/msg/image.hpp>

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<rclcpp::Node>("test_image_publisher");
    auto publisher = node->create_publisher<sensor_msgs::msg::Image>("/debug_img", rclcpp::QoS(1).reliable());

    constexpr uint32_t kWidth = 64;
    constexpr uint32_t kHeight = 48;
    uint8_t phase = 0;
    auto timer = node->create_wall_timer(std::chrono::milliseconds(100), [&]() {
        sensor_msgs::msg::Image image;
        image.header.stamp = node->now();
        image.header.frame_id = "camera";
        image.width = kWidth;
        image.height = kHeight;
        image.encoding = "rgb8";
        image.step = kWidth * 3;
        image.data.resize(static_cast<size_t>(image.step) * kHeight);
        for (uint32_t y = 0; y < kHeight; ++y) {
            for (uint32_t x = 0; x < kWidth; ++x) {
                uint8_t* pixel = &image.data[y * image.step + x * 3];
                pixel[0] = static_cast<uint8_t>(x * 4 + phase);
                pixel[1] = static_cast<uint8_t>(y * 5);
                pixel[2] = phase;
            }
        }
        ++phase;
        publisher->publish(image);
    });

    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
