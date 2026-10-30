#pragma once

#include <basin/log_util/log_manager.hpp>
#include <basin/log_util/log_writer.hpp>
#include <basin/log_util/snapshot_tracker.hpp>
#include <basin/node_core/node_core.hpp>
#include <chrono>
#include <filesystem>
#include <functional>
#include <logging_msgs/msg/log_metadata.hpp>
#include <logging_msgs/msg/logger_status.hpp>
#include <logging_msgs/srv/snapshot_request.hpp>
#include <map>
#include <memory>
#include <rclcpp/node.hpp>
#include <regex>
#include <string>
#include <vector>

#include "continous_logger/logger_node_params.hpp"

namespace basin::continuous_logger {

struct LoggedTopic {
    ///< Name of the topic
    std::string topic_name{};

    ///< Name of the topic type
    std::string topic_type{};

    ///< Current counter used for throttling
    int throttle_count{ 0 };

    ///< Upper limit of the throttle counter
    int max_throttle{ 0 };

    ///< Generic subscription to the topic
    rclcpp::GenericSubscription::SharedPtr subscription{ nullptr };

    ///< Callback function pointer for the message data
    std::function<void(const std::shared_ptr<const rclcpp::SerializedMessage>)> callback{};
};

/// @brief A configured topic name containing `*`. Each `*` matches one name segment, so `/*/driver/camera_info`
/// matches `/front/driver/camera_info` but not `/a/b/driver/camera_info`
struct TopicPattern {
    ///< The pattern as configured
    std::string pattern{};

    ///< The pattern compiled to a regex
    std::regex regex{};

    ///< Upper limit of the throttle counter for topics matching this pattern
    int max_throttle{ 0 };
};

class LoggerNode : public basin::node_core::NodeCore {
   public:
    explicit LoggerNode(const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

    ~LoggerNode();

   private:
    /// @brief Attempt to discover the topics we have not yet found. Will loop until all requested topics are
    /// discovered and subscribed, or forever when a topic pattern is configured so later topics are picked up
    void discoverTopics();

    /// @brief Callback function for receiving new log metadata. This handles both events and metadata
    /// @param msg The metadata message to add to the log
    void metadataCallback(logging_msgs::msg::LogMetadata::ConstSharedPtr msg);

    /// @brief Callback function used for all logged messages. This automatically updates the log as needed
    /// @param msg The RCL serialized message to write into the log
    /// @param topic The name of the topic the message originated from
    void genericCallback(std::shared_ptr<const rclcpp::SerializedMessage> msg, LoggedTopic& topic);

    /// @brief Opens this run's log in the log directory, named by the log manager
    void openNextLogfile();

    /// @brief Processes a snapshot request from the snapshot server
    /// @param req The request information for the snapshot
    /// @param resp The response information for the snapshot request
    void handleSnapshotRequest(logging_msgs::srv::SnapshotRequest::Request::SharedPtr req,
                               logging_msgs::srv::SnapshotRequest::Response::SharedPtr resp);

    /// @brief Timer callback that has the log manager rotate the oldest unprotected logs and measure the disk, then
    /// reports what it found. While snapshot-protected logs fill at least snapshot_usage_threshold_percent of the
    /// disk it reports the fault `continuous_logger` / `disk_usage_high`. That fault expires after two check
    /// intervals, so it clears once the checks stop reporting it
    void checkDiskUsage();

    /// @brief Processes event-triggered snapshots with time windows
    /// @param event_name The name of the event that occurred
    /// @param event_time The time the event occurred
    void handleEventSnapshot(const std::string& event_name, const rclcpp::Time& event_time);

    /// @brief Tick callback: finalizes event snapshots whose time_after window has ended and publishes the status
    void onTick();

    /// @brief Builds the current LoggerStatus
    /// @param now The current time, used to stamp the status
    logging_msgs::msg::LoggerStatus buildStatus(const rclcpp::Time& now) const;

    /// @brief Emit a manifest IPC file for a completed snapshot
    /// @param snapshot_id The ID of the snapshot to emit a manifest for
    void emitSnapshotManifest(const std::string& snapshot_id);

    /// @brief Cleanup resources during shutdown
    void shutdown();

    ///< Node parameters for configuration
    std::unique_ptr<LoggerParams> params_;

    ///< Generic topic subscriptions for capturing data, keyed by topic name. A map so each entry stays put
    ///< while discovery adds more, as its subscription's callback holds a reference to it
    std::map<std::string, LoggedTopic> topics_;

    ///< Configured topic patterns, matched against every topic discovery sees
    std::vector<TopicPattern> topic_patterns_;

    ///< Log writing instance
    std::unique_ptr<log_util::LogWriter> log_writer_;

    ///< Snapshot lifecycle and WAL tracking
    std::unique_ptr<log_util::SnapshotTracker> snapshot_tracker_;

    ///< Log manager instance
    std::unique_ptr<log_util::LogManager> log_manager_;

    ///< ROS Subscription for capturing metadata in a log file
    rclcpp::Subscription<logging_msgs::msg::LogMetadata>::SharedPtr metadata_sub_;

    ///< ROS service for handling Snapshot requests
    rclcpp::Service<logging_msgs::srv::SnapshotRequest>::SharedPtr snapshot_service_;

    ///< Publisher for the LoggerStatus, sent every tick
    rclcpp::Publisher<logging_msgs::msg::LoggerStatus>::SharedPtr status_pub_;

    ///< Timer for attempting to discover each of the logged topic types
    rclcpp::TimerBase::SharedPtr discovery_timer_;

    ///< Timer for checking disk usage periodically
    rclcpp::TimerBase::SharedPtr disk_check_timer_;

    ///< Timer for forcing periodic log file splits independent of message flow
    rclcpp::TimerBase::SharedPtr split_timer_;

    ///< Resolved manifest directory (defaults to {log_path}/pending)
    std::filesystem::path manifest_dir_;
};

}  // namespace basin::continuous_logger
