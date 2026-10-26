#include <iostream>
#include <memory>
#include <rclcpp/rclcpp.hpp>

#include "pcap_recorder/pcap_recorder_node.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);

    try {
        auto node = std::make_shared<basin::pcap_recorder::PcapRecorderNode>();
        rclcpp::spin(node);
        node->shutdown();
        rclcpp::shutdown();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        rclcpp::shutdown();
        return 1;
    }
}
