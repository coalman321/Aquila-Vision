#pragma once

#include <basin/node_core/node_params.hpp>
#include <rclcpp/node.hpp>

namespace basin::launch_core {

struct LaunchNodeParams : public basin::node_core::NodeParams {
    // Launch file path discovery
    std::string launch_file{ "test.prod.py" };
    std::string launch_package{ "launch_api" };

    // Optional daemon file discovery
    std::string daemon_file{ "" };
    std::string daemon_package{ "" };

    long num_restarts{ 5 };

    double watchdog_delay_s{ 6.0 };

    std::string subsystem_postfix{ "" };

    LaunchNodeParams(rclcpp::Node* node) : basin::node_core::NodeParams(node) {
        createParam("launch_file", launch_file);
        createParam("launch_package", launch_package);

        createParam("daemon_file", daemon_file);
        createParam("daemon_package", daemon_package);

        createParam("num_restarts", num_restarts);
        createParam("watchdog_delay_s", watchdog_delay_s);

        createParam("subsystem_postfix", subsystem_postfix);
    }
};

}  // namespace basin::launch_core
