#pragma once

#include <atomic>
#include <basin/log_util/log_manager.hpp>
#include <basin/web_core/web_node.hpp>
#include <chrono>
#include <logging_msgs/msg/logger_status.hpp>
#include <logging_msgs/srv/snapshot_request.hpp>
#include <memory>
#include <mutex>
#include <optional>
#include <rclcpp/executors/single_threaded_executor.hpp>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <unordered_map>

namespace camera_web::logger {

/// @brief Configuration for the LoggerHandler component.
struct LoggerHandlerConfig {
    /// ROS 2 topic the continuous logger publishes its LoggerStatus on
    std::string status_topic{ "/log/logger_status" };
    /// ROS 2 service name of the continuous logger's snapshot service
    std::string snapshot_service{ "/log/snapshot" };
    /// Timeout when waiting for the snapshot service response
    std::chrono::milliseconds service_timeout{ std::chrono::seconds(5) };
    /// A status older than this means the logger is not running
    std::chrono::milliseconds stale_after{ std::chrono::seconds(2) };
    /// The continuous logger's log directory, read directly to download snapshots
    std::string log_dir{ "/var/lib/aquila/logs" };
    /// Most snapshot downloads served at once; each reads its logs from disk twice
    int max_concurrent_downloads{ 2 };
};

/// @brief Web API component that exposes the continuous logger's status and
///        snapshot service via a REST interface.
///
/// REST endpoints
/// --------------
///   GET  /api/logger/status
///       Returns the most recently received LoggerStatus as JSON:
///         {
///           "connected": <bool>,   false when no status arrived within stale_after
///           "log_path": "...",
///           "disk": { "total_bytes", "free_bytes", "usage_percent", "rotation_threshold_percent",
///                     "log_bytes", "snapshot_protected_bytes", "snapshot_usage_percent",
///                     "snapshot_usage_threshold_percent" },
///           "snapshots": [ { "id", "trigger", "description", "tags", "start_time", "end_time",
///                            "state": "running" | "pending" | "complete" } ],
///           "total_snapshot_count": <uint>,
///           "events": [ { "event_name", "time_before", "time_after", "trigger_count" } ]
///         }
///
///   POST /api/logger/snapshot
///       Body (required JSON):
///         {
///           "name":        "...",                 (required) [A-Za-z0-9_-], 1-64 characters
///           "duration_s":  30 | 60 | 120 | 300,   (required) seconds before now to capture
///           "description": "...",
///           "tags":        ["..."]
///         }
///       Captures the last duration_s seconds as a complete snapshot. Responds
///       200 on success, 409 if the name is taken, 400 on a bad request and
///       504 if the logger is unavailable.
///
///   DELETE /api/logger/snapshot?id=<snapshot id>
///       Removes a complete snapshot, so rotation may delete its logs once it
///       needs the space. The logs themselves are kept. Responds 200 on
///       success, 404 for an unknown snapshot, 409 for one still recording or
///       being uploaded, and 504 if the logger is unavailable.
///
///   GET  /api/logger/snapshot/download?id=<snapshot id>
///       Streams <id>.zip: the snapshot's MCAP files, whole, as they are on
///       disk (every split overlapping its window, the same files the logger
///       protects for it), plus a snapshot.yaml of its metadata. Read straight
///       from log_dir, so it works without the logger running. Responds 404
///       for an unknown snapshot or one whose logs are gone, 409 for one still
///       recording, and 429 when max_concurrent_downloads are in progress.
///       HEAD runs the same checks without building the archive. With
///       &progress=<token> ([A-Za-z0-9_-], 8-64 characters, picked by the
///       client), the download's progress can be read from the endpoint below.
///
///   GET  /api/logger/snapshot/download/progress?token=<token>
///       The progress of the download started with that token:
///         {
///           "phase": "preparing" | "sending" | "done" | "stopped" | "failed",
///           "done_bytes": <uint>,  "total_bytes": <uint>,  of the current phase
///           "error": "..."         when failed
///         }
///       Preparing checksums every log before the first byte can be sent, so
///       it can take a while for a long snapshot. Responds 404 for a token
///       not (or no longer) known; finished downloads are forgotten after a
///       minute.
///
/// Service calls are fully synchronous from the HTTP handler's perspective;
/// a dedicated SingleThreadedExecutor spins the service client's callback
/// group so that the main node executor is never blocked.
class LoggerHandler {
   public:
    using LoggerStatus = logging_msgs::msg::LoggerStatus;
    using SnapshotRequest = logging_msgs::srv::SnapshotRequest;

    LoggerHandler(std::shared_ptr<basin::web_core::WebNode> web_node, LoggerHandlerConfig config = {});

    /// @brief Returns the latest logger status, or nothing if it is missing or stale (thread-safe).
    std::optional<LoggerStatus> getLatestStatus() const;

   private:
    /// @brief Send a SnapshotRequest synchronously from an HTTP handler thread.
    /// @return shared_ptr to the response, or nullptr on timeout/error.
    std::shared_ptr<SnapshotRequest::Response> callService(std::shared_ptr<SnapshotRequest::Request> request);

    /// @brief Handles GET and HEAD of /api/logger/snapshot/download
    void handleDownload(const httplib::Request& req, httplib::Response& res);

    /// @brief A download's progress, as reported by /api/logger/snapshot/download/progress
    struct DownloadProgress {
        std::string phase;
        uint64_t done_bytes{ 0 };
        uint64_t total_bytes{ 0 };
        std::string error;
        std::chrono::steady_clock::time_point updated;
    };

    /// @brief Records a download's progress under its token, if it has one (thread-safe)
    void setProgress(const std::string& token, const std::string& phase, uint64_t done_bytes,
                     uint64_t total_bytes, const std::string& error = "");

    // --- ROS 2 interfaces ---
    rclcpp::Client<SnapshotRequest>::SharedPtr snapshot_client_;
    rclcpp::Subscription<LoggerStatus>::SharedPtr status_sub_;

    /// Dedicated executor for spinning just the service-client callback group.
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> client_executor_;
    rclcpp::CallbackGroup::SharedPtr client_cbg_;

    /// Serializes service calls, as the dedicated executor can only be spun by one thread at a time.
    std::mutex service_mutex_;

    // --- Latest status (protected by mutex for cross-thread access) ---
    mutable std::mutex status_mutex_;
    std::optional<LoggerStatus> latest_status_{};
    std::chrono::steady_clock::time_point latest_status_time_{};

    // --- Snapshot downloads ---
    /// Reads the log directory to find each snapshot's logs. Only ever scanned and queried, never asked to
    /// rotate, so it changes nothing the logger owns
    basin::log_util::LogManager log_manager_;
    std::atomic<int> active_downloads_{ 0 };
    /// Progress of the downloads started with a token, by token
    std::mutex progress_mutex_;
    std::unordered_map<std::string, DownloadProgress> progress_;

    LoggerHandlerConfig config_;
    rclcpp::Logger logger_{ rclcpp::get_logger("LoggerHandler") };
};

}  // namespace camera_web::logger
