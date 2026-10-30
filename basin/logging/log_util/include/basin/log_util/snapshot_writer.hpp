#pragma once

#include <map>
#include <mutex>
#include <rclcpp/rclcpp.hpp>
#include <string>
#include <vector>

namespace basin::log_util {

/// @brief Information about a single snapshot
struct SnapshotInfo {
    std::string id;
    int64_t start_time;  // epoch seconds
    int64_t end_time;    // epoch seconds
    std::string trigger;
    std::string description;
    std::vector<std::string> tags;
};

/// @brief WAL entry for a manually-started snapshot that has not yet been stopped
struct WalOpenSnapshot {
    std::string id;
    int64_t start_time;
    std::string trigger;
    std::string description;
    std::vector<std::string> tags;
};

/// @brief WAL entry for an event-triggered snapshot waiting for its finalize_time
struct WalPendingEventSnapshot {
    std::string snapshot_id;
    std::string event_name;
    int64_t finalize_time_sec;
    int32_t finalize_time_nsec;
};

/// @brief Aggregated WAL state written to disk on every transition
struct WalState {
    std::vector<WalOpenSnapshot> open_snapshots;
    std::vector<WalPendingEventSnapshot> pending_event_snapshots;
};

/// @brief Handles writing snapshot metadata to a YAML file
///
/// This component manages snapshot files stored in the recording directory.
/// Snapshots are written as a separate YAML file containing:
/// - Snapshot ID
/// - Start and end times (epoch seconds)
/// - Trigger type (manual or event-based)
/// - Description
/// - Tags for categorization
class SnapshotWriter {
   public:
    SnapshotWriter();
    ~SnapshotWriter();

    /// @brief Sets the recording directory where snapshots will be stored
    /// @param directory Path to the recording directory
    void set_recording_directory(const std::string& directory);

    /// @brief Starts a new snapshot
    /// @param snapshot_id Unique identifier for the snapshot
    /// @param trigger The trigger type (e.g., "manual" or "event-<trigger_id>")
    /// @param description User-defined description
    /// @param tags List of tags for categorization
    /// @param start_time_override Optional: override the start time (default: current time)
    /// @return true if started successfully, false if snapshot already exists
    bool start_snapshot(const std::string& snapshot_id, const std::string& trigger = "manual",
                        const std::string& description = "", const std::vector<std::string>& tags = {},
                        int64_t start_time_override = -1);

    /// @brief Stops an active snapshot
    /// @param snapshot_id The ID of the snapshot to stop
    /// @param end_time_override Optional: override the end time (default: current time)
    /// @return true if stopped successfully, false if snapshot doesn't exist or isn't running
    bool stop_snapshot(const std::string& snapshot_id, int64_t end_time_override = -1);

    /// @brief Removes a completed snapshot, so it no longer protects any logs. The logs themselves are kept
    /// @param snapshot_id The ID of the snapshot to remove
    /// @return true if removed and saved, false if the snapshot doesn't exist or is still running
    bool remove_snapshot(const std::string& snapshot_id);

    /// @brief Checks if a snapshot exists (running or completed)
    /// @param snapshot_id The ID to check
    /// @return true if the snapshot exists
    bool has_snapshot(const std::string& snapshot_id) const;

    /// @brief Checks if a snapshot is currently running
    /// @param snapshot_id The ID to check
    /// @return true if the snapshot is running (not yet stopped)
    bool is_snapshot_running(const std::string& snapshot_id) const;

    /// @brief Gets information about a specific snapshot
    /// @param snapshot_id The ID of the snapshot
    /// @return The snapshot information, or empty SnapshotInfo if not found
    SnapshotInfo get_snapshot(const std::string& snapshot_id) const;

    /// @brief Gets all snapshots
    /// @return Vector of all snapshot information
    std::vector<SnapshotInfo> get_all_snapshots() const;

    /// @brief Loads existing snapshots from the YAML file
    /// @return true if loaded successfully
    bool load_from_file();

    /**
     * @brief Ensures the snapshots file exists, creating an empty one if needed
     * @return true if file exists or was created successfully
     */
    bool ensure_file_exists();

    /**
     * @brief Saves all snapshots to the YAML file atomically (tmp → fdatasync → rename)
     * @return true if saved successfully
     */
    bool save_to_file();

    /// @brief Atomically writes the WAL state to snapshot_wal.yaml
    /// @param state The current WAL state to persist
    /// @return true if written successfully
    bool save_wal(const WalState& state);

    /// @brief Loads the WAL state from snapshot_wal.yaml; recovers from .tmp if main is absent
    /// @param state Output parameter populated with the loaded WAL state
    /// @return true if a WAL file was found and loaded, false if no WAL exists
    bool load_wal(WalState& state);

    /// @brief Deletes snapshot_wal.yaml and its .tmp variant
    /// @return true if the files were removed (or did not exist)
    bool clear_wal();

   private:
    mutable std::mutex mutex_;
    std::string recording_directory_;
    std::map<std::string, SnapshotInfo> snapshots_;

    static constexpr int SNAPSHOT_FILE_VERSION = 1;
    static constexpr const char* SNAPSHOT_FILENAME = "snapshots.yaml";

    static constexpr int WAL_FILE_VERSION = 1;
    static constexpr const char* WAL_FILENAME = "snapshot_wal.yaml";
};

}  // namespace basin::log_util
