#pragma once

#include <filesystem>
#include <mutex>
#include <rclcpp/rclcpp.hpp>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "basin/log_util/snapshot_writer.hpp"

namespace basin::log_util {

/// @brief Manages log storage on disk
///
/// This component manages the log directory and handles:
/// - Monitoring log directory for individual MCAP files
/// - Monitoring snapshot file for snapshots
/// - Providing interface for querying snapshots
/// - Rotating old MCAP files based on disk capacity
/// - Ensuring snapshot-protected MCAP files are preserved
class LogManager {
   public:
    LogManager();
    ~LogManager();

    /// @brief Sets the directory to scan for manifest files (for file-protection)
    /// @param dir Path to the manifest directory
    void set_manifest_dir(const std::string& dir);

    /// @brief Sets the log directory to manage
    /// @param directory Path to the log directory
    void set_log_directory(const std::string& directory);

    /// @brief Sets disk usage limits
    /// @param max_usage_percent Maximum disk usage percentage (0-100)
    /// @param warning_threshold_percent Warning threshold percentage (0-100)
    void set_disk_limits(double max_usage_percent, double warning_threshold_percent);

    /// @brief Scans the log directory for MCAP files and snapshots
    /// @return Number of MCAP files found
    size_t scan_log_directory();

    /// @brief Checks disk usage and rotates old logs if needed
    /// @return true if rotation was performed, false otherwise
    bool check_and_rotate_logs();

    /// @brief Gets the list of all MCAP file paths
    /// @return Vector of MCAP file paths
    std::vector<std::string> get_log_files() const;

    /// @brief Gets MCAP files associated with a snapshot
    /// @param snapshot_id The snapshot ID
    /// @return Vector of MCAP file paths that overlap with the snapshot time range
    std::vector<std::string> get_snapshot_log_files(const std::string& snapshot_id) const;

    /// @brief Gets the current disk usage percentage
    /// @return Disk usage as a percentage (0-100)
    double get_disk_usage_percent() const;

    /// @brief Checks if a warning should be issued due to high disk usage
    /// @return true if disk usage exceeds warning threshold
    bool should_warn_disk_usage() const;

    /// @brief Gets the snapshot writer for querying snapshots
    /// @return Reference to the snapshot writer
    const SnapshotWriter& get_snapshot_writer() const;

   private:
    struct LogFileInfo {
        std::string path;
        std::filesystem::file_time_type last_write_time;
        uintmax_t size_bytes;
        bool associated_with_snapshot;
        int64_t start_time;  // epoch seconds
        int64_t end_time;    // epoch seconds
    };

    mutable std::mutex mutex_;
    std::string log_directory_;
    std::string manifest_dir_;
    double max_usage_percent_;
    double warning_threshold_percent_;
    std::vector<LogFileInfo> log_files_;
    SnapshotWriter snapshot_writer_;

    /// Cache of previously-scanned MCAP files; keyed by absolute path
    std::unordered_map<std::string, LogFileInfo> scan_cache_;

    /// Time-range windows [start, end] derived from manifest files in manifest_dir_
    std::vector<std::pair<int64_t, int64_t>> manifest_protected_ranges_;

    /**
     * @brief Determines if a MCAP file overlaps with any snapshot
     * @param log_file The log file info to check
     * @return true if the log file is associated with at least one snapshot
     */
    bool is_log_file_associated_with_snapshot(const LogFileInfo& log_file) const;

    /**
     * @brief Gets the time range for a MCAP file by examining its metadata
     * @param log_path Path to the MCAP file
     * @param start_time Output parameter for start time
     * @param end_time Output parameter for end time
     * @return true if times were successfully extracted
     */
    bool get_log_time_range(const std::string& log_path, int64_t& start_time, int64_t& end_time) const;

    /**
     * @brief Deletes the oldest MCAP file that is not associated with a snapshot
     * @return true if a log file was deleted
     */
    bool delete_oldest_unprotected_log();
};

}  // namespace basin::log_util
