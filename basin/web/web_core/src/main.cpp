#include <rclcpp/rclcpp.hpp>

#include "basin/web_core/file_manager_component.hpp"
#include "basin/web_core/web_node.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<basin::web_core::WebNode>("web_node", rclcpp::NodeOptions());
    // Use FileManagerComponent to expose a /upload endpoint
    basin::web_core::FileManagerComponentConfig upload_cfg;
    upload_cfg.base_route = "/upload";
    upload_cfg.files_dir = "/tmp/uploads";
    auto fm_upload = std::make_shared<basin::web_core::FileManagerComponent>(node, upload_cfg);
    node->startServer();
    rclcpp::spin(node);
    rclcpp::shutdown();
    return 0;
}
