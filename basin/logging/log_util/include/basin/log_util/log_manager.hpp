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

/// @brief The outcome of one disk check
struct DiskCheckResult {
    ///< Unprotected log files deleted by this check, oldest first
    size_t rotated_files{ 0 };

    ///< Usage of the log filesystem after rotating (0-100)
    double disk_usage_percent{ 0.0 };

    ///< Usage is still at or above the rotation threshold because every remaining log is protected
    bool rotation_blocked{ false };

    ///< Bytes in log files that a snapshot (running or complete) or a pending manifest protects
    uintmax_t protected_bytes{ 0 };

    ///< Bytes in all log files, protected or not
    uintmax_t log_bytes{ 0 };

    ///< protected_bytes as a percentage of the filesystem's capacity (0-100)
    double snapshot_usage_percent{ 0.0 };

    ///< snapshot_usage_percent is at or above the snapshot usage threshold: the disk is filling with logs that
    ///< rotation may not delete
    bool snapshot_usage_high{ false };
};

/// @brief Manages log storage on disk
///
/// This component manages the log directory and handles:
/// - Preparing the log directory and naming each run's log
/// - Monitoring log directory for individual MCAP files
/// - Monitoring snapshot file for snapshots
/// - Providing interface for querying snapshots
/// - Rotating the oldest MCAP files once disk usage reaches a threshold
/// - Ensuring snapshot-protected MCAP files are preserved, and reporting how much of the disk they fill
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
    /// @param rotation_threshold_percent Disk usage percentage (0-100) at which the oldest unprotected logs are
    /// deleted, until usage is back under it
    /// @param snapshot_usage_threshold_percent Percentage (0-100) of the disk that snapshot-protected logs may fill
    /// before a check reports snapshot_usage_high
    void set_disk_limits(double rotation_threshold_percent, double snapshot_usage_threshold_percent);

    /// @brief Creates the log directory if it is missing
    /// @throws std::runtime_error if the path exists but is not a directory
    void prepare_log_directory();

    /// @brief Name for this run's log: `prefix` followed by one more than the highest run number of any non-empty
    /// `<prefix><N>[_<split>].mcap` already in the log directory, or `<prefix>0` if there is none
    /// @param prefix The log name prefix, e.g. "log_"
    std::string next_log_name(const std::string& prefix) const;

    /// @brief Scans the log directory for MCAP files and snapshots
    /// @return Number of MCAP files found
    size_t scan_log_directory();

    /// @brief Rescans the log directory, deletes the oldest unprotected logs until disk usage is under the rotation
    /// threshold, and reports where the disk stands afterwards
    /// @param active_log_file Path of the MCAP file currently being written, which is never deleted
    DiskCheckResult check_disk(const std::string& active_log_file);

    /// @brief The result of the most recent check_disk, or a default one before the first check
    DiskCheckResult get_last_disk_check() const;

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

    /// @brief Gets the snapshot writer for querying snapshots
    /// @return Reference to the snapshot writer
    const SnapshotWriter& get_snapshot_writer() const;

   private:
    struct LogFileInfo {
        std::string path;
        std::filesystem::file_time_type last_write_time;
        uintmax_t size_bytes;
        bool associated_with_snapshot;
        int64_t start_time;  // epoch seconds, rounded down
        int64_t end_time;    // epoch seconds, rounded up
    };

    mutable std::mutex mutex_;
    std::string log_directory_;
    std::string manifest_dir_;
    double rotation_threshold_percent_;
    double snapshot_usage_threshold_percent_;
    std::vector<LogFileInfo> log_files_;
    SnapshotWriter snapshot_writer_;
    DiskCheckResult last_disk_check_;

    /// Cache of previously-scanned MCAP files; keyed by absolute path
    std::unordered_map<std::string, LogFileInfo> scan_cache_;

    /// Time-range windows [start, end] derived from manifest files in manifest_dir_
    std::vector<std::pair<int64_t, int64_t>> manifest_protected_ranges_;

    /// @brief scan_log_directory, with mutex_ already held
    size_t scan_log_directory_locked();

    /// @brief Total size of the filesystem the log directory is on, or 0 if it can't be read
    uintmax_t get_disk_capacity_bytes() const;

    /**
     * @brief Determines if a MCAP file overlaps with any snapshot, running or complete, or a pending manifest
     * @param log_file The log file info to check
     * @return true if the log file is associated with at least one snapshot
     */
    bool is_log_file_associated_with_snapshot(const LogFileInfo& log_file) const;

    /**
     * @brief Gets the time range for a MCAP file by examining its metadata. The range is widened to whole seconds
     * (start rounded down, end rounded up), so overlap checks err toward protecting a file
     * @param log_path Path to the MCAP file
     * @param start_time Output parameter for start time
     * @param end_time Output parameter for end time
     * @return true if times were successfully extracted
     */
    bool get_log_time_range(const std::string& log_path, int64_t& start_time, int64_t& end_time) const;

    /**
     * @brief Deletes the oldest MCAP file that is not associated with a snapshot
     * @param active_log_file Path of the MCAP file currently being written, which is never deleted
     * @return true if a log file was deleted
     */
    bool delete_oldest_unprotected_log(const std::string& active_log_file);
};

}  // namespace basin::log_util
