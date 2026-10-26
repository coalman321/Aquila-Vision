#pragma once

#include <basin/log_util/log_manager.hpp>
#include <basin/log_util/log_writer.hpp>
#include <basin/log_util/snapshot_writer.hpp>
#include <basin/node_core/node_core.hpp>
#include <chrono>
#include <filesystem>
#include <functional>
#include <logging_msgs/msg/log_metadata.hpp>
#include <logging_msgs/msg/logger_status.hpp>
#include <logging_msgs/srv/snapshot_request.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <rclcpp/node.hpp>
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

class LoggerNode : public basin::node_core::NodeCore {
   public:
    LoggerNode(const rclcpp::NodeOptions& options);

    ~LoggerNode();

   private:
    /// @brief Attempt to discover the topics we have not yet found. Will loop until
    /// all requested topics are discovered and subscribed
    void discoverTopics();

    /// @brief Callback function for receiving new log metadata. This handles both events and metadata
    /// @param msg The metadata message to add to the log
    void metadataCallback(logging_msgs::msg::LogMetadata::ConstSharedPtr msg);

    /// @brief Callback function used for all logged messages. This automatically updates the log as needed
    /// @param msg The RCL serialized message to write into the log
    /// @param topic The name of the topic the message originated from
    void genericCallback(std::shared_ptr<const rclcpp::SerializedMessage> msg, LoggedTopic& topic);

    /// @brief Handles loading the logfile and beginning the recording process. This also handles computing the name of
    /// the new log file to generate
    void openNextLogfile();

    /// @brief Processes a snapshot request from the snapshot server
    /// @param req The request information for the snapshot
    /// @param resp The response information for the snapshot request
    void handleSnapshotRequest(logging_msgs::srv::SnapshotRequest::Request::SharedPtr req,
                               logging_msgs::srv::SnapshotRequest::Response::SharedPtr resp);

    /// @brief Timer callback to check disk usage and rotate logs as needed. Once rotation is done it reports the
    /// fault `continuous_logger` / `disk_usage_high` if usage is still at or above the warning threshold. That
    /// fault expires after two check intervals, so it clears once the checks stop reporting it
    void checkDiskUsage();

    /// @brief Processes event-triggered snapshots with time windows
    /// @param event_name The name of the event that occurred
    /// @param event_time The time the event occurred
    void handleEventSnapshot(const std::string& event_name, const rclcpp::Time& event_time);

    /// @brief Timer callback to finalize event snapshots after time_after window
    void onTick();

    /// @brief Builds the current LoggerStatus. Must be called with event_snapshot_mutex_ held
    /// @param now The current time, used to stamp the status
    logging_msgs::msg::LoggerStatus buildStatusLocked(const rclcpp::Time& now) const;

    /// @brief Initialize event snapshot counters from existing snapshots
    void initializeEventSnapshotCounters();

    /// @brief Recover open and pending-event snapshots from the WAL after a crash
    void recoverFromWAL();

    /// @brief Emit a manifest IPC file for a completed snapshot
    /// @param snapshot_id The ID of the snapshot to emit a manifest for
    void emitSnapshotManifest(const std::string& snapshot_id);

    /// @brief Save the current WAL state (acquires event_snapshot_mutex_)
    void saveWal();

    /// @brief Save the current WAL state — must be called with event_snapshot_mutex_ held
    void saveWalLocked();

    /// @brief Cleanup resources during shutdown
    void shutdown();

    /// @brief Structure to track pending event snapshots
    struct PendingEventSnapshot {
        std::string snapshot_id;
        std::string event_name;
        rclcpp::Time trigger_time;
        rclcpp::Time finalize_time;
        std::vector<std::string> tags;
        std::string description;
    };

    ///< Node parameters for configuration
    std::unique_ptr<LoggerParams> params_;

    ///< List of generic topic subscriptions for capturing data
    std::vector<LoggedTopic> topics_;

    ///< Log writing instance
    std::unique_ptr<log_util::LogWriter> log_writer_;

    ///< Snapshot writing instance
    std::unique_ptr<log_util::SnapshotWriter> snapshot_writer_;

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

    ///< Timer for finalizing event snapshots
    rclcpp::TimerBase::SharedPtr event_snapshot_timer_;

    ///< Timer for forcing periodic log file splits independent of message flow
    rclcpp::TimerBase::SharedPtr split_timer_;

    ///< List of pending event snapshots waiting to be finalized
    std::vector<PendingEventSnapshot> pending_event_snapshots_;

    ///< Mutex for protecting pending_event_snapshots_ and event_snapshot_counters_
    std::mutex event_snapshot_mutex_;

    ///< Per-event-name counters for generating unique snapshot IDs
    std::map<std::string, uint64_t> event_snapshot_counters_;

    ///< Resolved manifest directory (defaults to {log_path}/pending)
    std::filesystem::path manifest_dir_;
};

}  // namespace basin::continuous_logger
