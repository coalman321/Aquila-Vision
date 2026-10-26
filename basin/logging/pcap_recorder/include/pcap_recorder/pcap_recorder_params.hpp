#pragma once

#include <basin/node_core/node_params.hpp>
#include <cstdint>
#include <filesystem>
#include <rclcpp/logging.hpp>
#include <rclcpp/node.hpp>
#include <stdexcept>
#include <string>
#include <vector>

namespace basin::pcap_recorder {

/// @brief Finds the tcpdump built with this package (@tcpdump//:tcpdump) in the runfiles of the running program:
/// those named by RUNFILES_DIR / RUNFILES_MANIFEST_FILE (bazel run, bazel test and the //basin tarball's launchers
/// set them), or else the ones next to the executable.
/// @param error Set to why, when it isn't found
/// @return The absolute path, or "" if it isn't found
std::string bundledTcpdumpPath(std::string* error);

struct PcapRecorderParams : public basin::node_core::NodeParams {
    // tcpdump: what to run and what it captures. See the README for how these become its arguments.
    // An empty path means the tcpdump bundled with the node (see bundledTcpdumpPath()).
    std::string tcpdump_path{ "" };
    std::string interface{ "any" };
    std::string filter{ "" };
    int snaplen{ 262144 };
    bool immediate_mode{ false };
    std::vector<std::string> extra_args{};
    double restart_delay{ 5.0 };

    // The in-memory buffer of traffic from before a trigger
    int64_t buffer_max_bytes{ 64 * 1024 * 1024 };
    double buffer_max_age{ 30.0 };

    // What a trigger publishes
    double capture_duration{ 10.0 };
    int64_t max_message_bytes{ 1024 * 1024 };
    int queue_depth{ 100 };

    // Fault patterns that start a capture: "*", "<fault_system>/*" or "<fault_system>/<fault_name>"
    std::vector<std::string> trigger_faults{ "*" };

    PcapRecorderParams(rclcpp::Node* node) : basin::node_core::NodeParams(node) {
        createParam("tcpdump.path", tcpdump_path);
        createParam("tcpdump.interface", interface);
        createParam("tcpdump.filter", filter);
        createParam("tcpdump.snaplen", snaplen);
        createParam("tcpdump.immediate_mode", immediate_mode);
        createParam("tcpdump.extra_args", extra_args);
        createParam("tcpdump.restart_delay", restart_delay);

        createParam("buffer.max_bytes", buffer_max_bytes);
        createParam("buffer.max_age", buffer_max_age);

        createParam("capture.duration", capture_duration);
        createParam("capture.max_message_bytes", max_message_bytes);
        createParam("capture.queue_depth", queue_depth);

        createParam("trigger.faults", trigger_faults);

        if (tcpdump_path.empty()) {
            std::string error;
            tcpdump_path = bundledTcpdumpPath(&error);
            if (tcpdump_path.empty()) {
                RCLCPP_FATAL_STREAM(node->get_logger(), "No bundled tcpdump (" << error << "); set tcpdump.path");
                throw std::runtime_error("bundled tcpdump not found: " + error);
            }
        }
        if (!std::filesystem::exists(tcpdump_path)) {
            RCLCPP_FATAL_STREAM(node->get_logger(), "tcpdump binary " << tcpdump_path << " does not exist!");
            throw std::runtime_error("tcpdump binary path does not exist");
        }
        if (interface.empty()) {
            throw std::runtime_error("tcpdump.interface must not be empty");
        }
        if (snaplen < 0) {
            throw std::runtime_error("tcpdump.snaplen must be >= 0");
        }
        if (buffer_max_bytes <= 0 || max_message_bytes <= 0) {
            throw std::runtime_error("buffer.max_bytes and capture.max_message_bytes must be positive");
        }
        if (buffer_max_age < 0.0 || capture_duration < 0.0 || restart_delay < 0.0) {
            throw std::runtime_error("buffer.max_age, capture.duration and tcpdump.restart_delay must be >= 0");
        }
        if (queue_depth <= 0) {
            throw std::runtime_error("capture.queue_depth must be positive");
        }
    }
};

/// @brief The argument list (argv[1..]) tcpdump is started with: write pcap to stdout, flushed per packet, then
/// the user's extra arguments, then the filter expression.
inline std::vector<std::string> buildTcpdumpArgs(const PcapRecorderParams& params) {
    std::vector<std::string> args{ "-i", params.interface, "-s", std::to_string(params.snaplen), "-U", "-w", "-" };
    if (params.immediate_mode) {
        args.push_back("--immediate-mode");
    }
    args.insert(args.end(), params.extra_args.begin(), params.extra_args.end());
    if (!params.filter.empty()) {
        args.push_back(params.filter);
    }
    return args;
}

/// @brief Whether a fault matches one of `patterns` ("*", "<system>/*" or "<system>/<name>").
inline bool faultMatches(const std::vector<std::string>& patterns, const std::string& fault_system,
                         const std::string& fault_name) {
    for (const std::string& pattern : patterns) {
        if (pattern == "*" || pattern == fault_system + "/*" || pattern == fault_system + "/" + fault_name) {
            return true;
        }
    }
    return false;
}

}  // namespace basin::pcap_recorder
