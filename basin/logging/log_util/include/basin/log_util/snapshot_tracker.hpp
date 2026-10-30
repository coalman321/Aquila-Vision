#pragma once

#include <cstdint>
#include <map>
#include <mutex>
#include <rclcpp/time.hpp>
#include <set>
#include <string>
#include <vector>

#include "basin/log_util/snapshot_writer.hpp"

namespace basin::log_util {

/// @brief Tracks manual and event-triggered snapshots through their lifetime, and keeps the snapshot WAL in step
///
/// Wraps a SnapshotWriter (which persists snapshots.yaml and the WAL file format) and owns the policy around it:
/// - Manual snapshots, started and stopped on request
/// - Event snapshots, which cover a window around an event and are finalized once its time_after has passed,
///   named `event_<name>_<n>` from a per-event counter that continues across runs
/// - The WAL, rewritten on every transition, so a crash never leaves a snapshot open for good
/// - Recovery from that WAL on startup
///
/// Times passed in are compared by their nanoseconds alone, so a recovered finalize time and the caller's clock
/// never need to share a clock type. All methods are thread safe.
class SnapshotTracker {
   public:
    /// @brief Sets the directory snapshots.yaml and the WAL live in, loads any existing snapshots, and creates an
    /// empty snapshots.yaml if there is none
    /// @param directory Path to the log directory
    void set_recording_directory(const std::string& directory);

    /// @brief Recovers from the WAL left by a previous run: stops the manual snapshots it says were open, finalizes
    /// event snapshots whose window has ended and re-arms the rest. Also seeds the per-event counters from the IDs of
    /// existing snapshots
    /// @param now The current wall-clock time; it ends recovered manual snapshots and decides which events are due
    /// @return The IDs of the snapshots it finalized, which are now complete
    std::vector<std::string> recover(const rclcpp::Time& now);

    /// @brief Starts a manual snapshot
    /// @return false if a snapshot with this ID already exists or it couldn't be saved
    bool start_manual(const std::string& id, const std::string& description, const std::vector<std::string>& tags);

    /// @brief Stops a running manual snapshot at the current time
    /// @return false if no snapshot with this ID is running or it couldn't be saved
    bool stop_manual(const std::string& id);

    /// @brief Starts an event snapshot covering [event_time - time_before, event_time + time_after]. It stays
    /// pending until finalize_due sees its window end
    /// @param event_name The event, which names the snapshot `event_<event_name>_<n>`
    /// @param event_time When the event happened
    /// @param time_before Seconds before the event to include
    /// @param time_after Seconds after the event to include
    /// @param description The snapshot description
    /// @param tags The snapshot tags
    /// @return The snapshot's ID, or an empty string if it couldn't be started
    std::string start_event(const std::string& event_name, const rclcpp::Time& event_time, double time_before,
                            double time_after, const std::string& description, const std::vector<std::string>& tags);

    /// @brief Finalizes the pending event snapshots whose window has ended
    /// @param now The current time
    /// @return The IDs of the snapshots it finalized
    std::vector<std::string> finalize_due(const rclcpp::Time& now);

    /// @brief Finalizes every pending event snapshot now, whether its window has ended or not, for shutdown. Manual
    /// snapshots stay running in the WAL, so the next run's recover closes them
    /// @param now The current time, which ends each snapshot
    /// @return The IDs of the snapshots it finalized
    std::vector<std::string> finalize_pending(const rclcpp::Time& now);

    /// @brief Checks if a snapshot exists, running or complete
    bool has_snapshot(const std::string& id) const;

    /// @brief Checks if a snapshot is running (started and not yet stopped)
    bool is_running(const std::string& id) const;

    /// @brief Gets a snapshot, or an empty SnapshotInfo if there is none with this ID
    SnapshotInfo get_snapshot(const std::string& id) const;

    /// @brief Gets every snapshot, from this run and earlier ones
    std::vector<SnapshotInfo> get_all_snapshots() const;

    /// @brief IDs of the event snapshots still inside their time_after window
    std::set<std::string> pending_event_ids() const;

    /// @brief Per-event-name counters: each is the `<n>` the event's next snapshot will get
    std::map<std::string, uint64_t> event_counters() const;

    /// @brief Persists snapshots.yaml
    bool save();

   private:
    /// @brief An event snapshot waiting for its time_after window to end
    struct PendingEvent {
        std::string snapshot_id;
        std::string event_name;
        int64_t finalize_time_ns;
    };

    /// @brief Rewrites the WAL from the running manual snapshots and the pending events. Call with mutex_ held
    void save_wal_locked();

    /// @brief Seeds event_counters_ from existing `event_<name>_<n>` IDs. Call with mutex_ held
    void initialize_event_counters_locked();

    mutable std::mutex mutex_;
    SnapshotWriter writer_;
    std::vector<PendingEvent> pending_events_;
    std::map<std::string, uint64_t> event_counters_;
};

}  // namespace basin::log_util
