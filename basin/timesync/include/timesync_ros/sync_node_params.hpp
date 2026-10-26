#pragma once

#include <basin/node_core/node_params.hpp>
#include <filesystem>
#include <rclcpp/logger.hpp>
#include <rclcpp/node.hpp>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace timesync_ros {

struct SyncNodeParams : public basin::node_core::NodeParams {
    fs::path phc2sys_path{ "/usr/sbin/phc2sys" };
    fs::path ptp4l_path{ "/usr/sbin/ptp4l" };

    bool is_grandmaster{ false };
    bool use_software_stamps{ false };
    int max_sync_diff_ns{ 2500000 };
    int out_of_sync_ticks{ 80 };

    std::vector<std::string> interfaces{ "eth0" };

    SyncNodeParams(rclcpp::Node* node) : basin::node_core::NodeParams(node) {
        createParam("phc2sys_path", phc2sys_path);
        createParam("ptp4l_path", ptp4l_path);

        createParam("is_grandmaster", is_grandmaster);
        createParam("use_software_stamps", use_software_stamps);
        createParam("max_sync_diff_ns", max_sync_diff_ns);
        createParam("out_of_sync_ticks", out_of_sync_ticks);

        createParam("interfaces", interfaces);

        // validate ptp binaries exist
        if (!fs::exists(ptp4l_path)) {
            RCLCPP_FATAL_STREAM(node->get_logger(), "PTP binary path " << ptp4l_path << " does not exist!");
            throw std::runtime_error("ptp4l binary path does not exist");
        }
        if (!fs::exists(phc2sys_path)) {
            RCLCPP_FATAL_STREAM(node->get_logger(), "PTP binary path " << phc2sys_path << " does not exist!");
            throw std::runtime_error("phc2sys binary path does not exist");
        }

        // validate at least 1 network interface
        if (interfaces.empty()) {
            RCLCPP_FATAL(node->get_logger(), "No network interfaces specified");
            throw std::runtime_error("no network interfaces specified");
        }

        if (!is_grandmaster && interfaces.size() > 1) {
            RCLCPP_FATAL(node->get_logger(), "Slave mode does not allow multiple network interfaces");
            throw std::runtime_error("Specified multiple network interfaces in slave mode");
        }
    }
};

}  // namespace timesync_ros
