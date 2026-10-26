#pragma once

#include <basin/node_core/node_params.hpp>
#include <filesystem>
#include <rclcpp/node.hpp>
#include <string>
#include <vector>

namespace basin::continuous_logger {

struct TopicConfig {
    std::string topic_name{ "/rosout" };
    int throttle_count{ 0 };
};

struct EventSnapshotConfig {
    std::string event_name;         // Name of the event to trigger on
    double time_before{ 5.0 };      // Seconds before event to include
    double time_after{ 5.0 };       // Seconds after event to include
    std::vector<std::string> tags;  // Tags to apply to the snapshot
    std::string description;        // Description template (can use {event_name})
};

struct LoggerParams : public basin::node_core::NodeParams {
    std::vector<TopicConfig> topic_configs{};

    ///< Path to the log directory
    std::filesystem::path log_path{ "/tmp/log" };

    std::string log_prefix{ "log_" };

    ///< Time between split files (seconds)
    long log_split_time{ 5 };

    ///< Max log size in bytes
    long log_split_size{ std::numeric_limits<long>::max() };

    ///< Maximum disk usage percentage before rotating logs (0-100)
    double max_disk_usage_percent{ 70.0 };

    ///< Disk usage warning threshold percentage (0-100)
    double disk_warning_threshold_percent{ 80.0 };

    ///< Interval for checking disk usage (seconds)
    double disk_check_interval{ 60.0 };

    ///< Directory for manifest IPC files; defaults to {log_path}/pending at runtime when empty
    std::filesystem::path manifest_dir{ "" };

    ///< List of event snapshot configurations
    std::vector<EventSnapshotConfig> event_snapshot_configs{};

    LoggerParams(rclcpp::Node* node) : basin::node_core::NodeParams(node) {
        for (const auto& key : getSubkeys("topics")) {
            TopicConfig cfg;
            createParam("topics." + key + ".name", cfg.topic_name);
            createParam("topics." + key + ".throttle", cfg.throttle_count);
            topic_configs.push_back(cfg);
        }

        // Log file configurations
        createParam("log_path", log_path);
        createParam("log_prefix", log_prefix);
        createParam("log_split_time", log_split_time);
        createParam("log_split_size", log_split_size);
        createParam("manifest_dir", manifest_dir);

        // Disk management configurations
        createParam("max_disk_usage_percent", max_disk_usage_percent);
        createParam("disk_warning_threshold_percent", disk_warning_threshold_percent);
        createParam("disk_check_interval", disk_check_interval);

        // Event snapshot configurations
        for (const auto& key : getSubkeys("event_snapshots")) {
            EventSnapshotConfig cfg;
            createParam("event_snapshots." + key + ".event_name", cfg.event_name);
            createParam("event_snapshots." + key + ".time_before", cfg.time_before);
            createParam("event_snapshots." + key + ".time_after", cfg.time_after);
            createParam("event_snapshots." + key + ".tags", cfg.tags);
            createParam("event_snapshots." + key + ".description", cfg.description);
            event_snapshot_configs.push_back(cfg);
        }
    }
};

}  // namespace basin::continuous_logger
