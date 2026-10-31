#include "basin/log_util/snapshot_tracker.hpp"

#include <algorithm>
#include <cmath>

namespace basin::log_util {

static constexpr int64_t NS_PER_S = 1000000000LL;

static constexpr const char* MANUAL_TRIGGER = "manual";

/// Whole epoch seconds of a time, rounded down, as snapshots store them
static int64_t toSeconds(int64_t ns) { return ns >= 0 ? ns / NS_PER_S : -((-ns + NS_PER_S - 1) / NS_PER_S); }

void SnapshotTracker::set_recording_directory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    writer_.set_recording_directory(directory);
    writer_.load_from_file();

    // Ensure snapshots.yaml exists even if empty, which makes it easier for tools to validate the log directory
    writer_.ensure_file_exists();
}

std::vector<std::string> SnapshotTracker::recover(const rclcpp::Time& now) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> finalized;
    WalState wal;
    if (writer_.load_wal(wal)) {
        int64_t now_ns = now.nanoseconds();

        // Manual snapshots that were open when the last run stopped end now
        for (const WalOpenSnapshot& open : wal.open_snapshots) {
            if (writer_.is_snapshot_running(open.id) && writer_.stop_snapshot(open.id, toSeconds(now_ns))) {
                finalized.push_back(open.id);
            }
        }

        // Event snapshots end at their finalize time: now if that has passed, otherwise once finalize_due sees it
        for (const WalPendingEventSnapshot& wal_pending : wal.pending_event_snapshots) {
            int64_t finalize_ns = wal_pending.finalize_time_sec * NS_PER_S + wal_pending.finalize_time_nsec;
            if (finalize_ns <= now_ns) {
                if (writer_.stop_snapshot(wal_pending.snapshot_id, wal_pending.finalize_time_sec)) {
                    finalized.push_back(wal_pending.snapshot_id);
                }
            } else {
                pending_events_.push_back(PendingEvent{ wal_pending.snapshot_id, wal_pending.event_name, finalize_ns });
            }
        }

        save_wal_locked();
    }

    initialize_event_counters_locked();
    return finalized;
}

bool SnapshotTracker::start_manual(const std::string& id, const std::string& description,
                                   const std::vector<std::string>& tags) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!writer_.start_snapshot(id, MANUAL_TRIGGER, description, tags)) {
        return false;
    }
    save_wal_locked();
    return true;
}

bool SnapshotTracker::stop_manual(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!writer_.is_snapshot_running(id) || !writer_.stop_snapshot(id)) {
        return false;
    }
    save_wal_locked();
    return true;
}

bool SnapshotTracker::capture_manual(const std::string& id, const rclcpp::Time& now, double time_before,
                                     const std::string& description, const std::vector<std::string>& tags) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Its window is already on disk, so it never runs and the WAL is left as it is
    int64_t now_ns = now.nanoseconds();
    int64_t start_time_sec = toSeconds(now_ns - static_cast<int64_t>(std::llround(time_before * NS_PER_S)));
    if (!writer_.start_snapshot(id, MANUAL_TRIGGER, description, tags, start_time_sec)) {
        return false;
    }
    return writer_.stop_snapshot(id, toSeconds(now_ns));
}

bool SnapshotTracker::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    // Only complete snapshots, which the WAL never lists, so it is left as it is
    return writer_.remove_snapshot(id);
}

std::string SnapshotTracker::start_event(const std::string& event_name, const rclcpp::Time& event_time,
                                         double time_before, double time_after, const std::string& description,
                                         const std::vector<std::string>& tags) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::string snapshot_id = "event_" + event_name + "_" + std::to_string(event_counters_[event_name]++);

    int64_t event_ns = event_time.nanoseconds();
    int64_t start_time_sec = toSeconds(event_ns - static_cast<int64_t>(std::llround(time_before * NS_PER_S)));
    int64_t finalize_ns = event_ns + static_cast<int64_t>(std::llround(time_after * NS_PER_S));

    if (!writer_.start_snapshot(snapshot_id, "event-" + event_name, description, tags, start_time_sec)) {
        return "";
    }

    pending_events_.push_back(PendingEvent{ snapshot_id, event_name, finalize_ns });
    save_wal_locked();
    return snapshot_id;
}

std::vector<std::string> SnapshotTracker::finalize_due(const rclcpp::Time& now) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> finalized;
    int64_t now_ns = now.nanoseconds();
    for (auto it = pending_events_.begin(); it != pending_events_.end();) {
        if (now_ns < it->finalize_time_ns) {
            ++it;
            continue;
        }

        // A snapshot that can't be stopped is dropped too, rather than retried every tick
        if (writer_.stop_snapshot(it->snapshot_id, toSeconds(it->finalize_time_ns))) {
            finalized.push_back(it->snapshot_id);
        }
        it = pending_events_.erase(it);
    }

    if (!finalized.empty()) {
        save_wal_locked();
    }
    return finalized;
}

std::vector<std::string> SnapshotTracker::finalize_pending(const rclcpp::Time& now) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> finalized;
    for (const PendingEvent& pending : pending_events_) {
        if (writer_.stop_snapshot(pending.snapshot_id, toSeconds(now.nanoseconds()))) {
            finalized.push_back(pending.snapshot_id);
        }
    }
    pending_events_.clear();

    save_wal_locked();
    return finalized;
}

bool SnapshotTracker::has_snapshot(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return writer_.has_snapshot(id);
}

bool SnapshotTracker::is_running(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return writer_.is_snapshot_running(id);
}

SnapshotInfo SnapshotTracker::get_snapshot(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return writer_.get_snapshot(id);
}

std::vector<SnapshotInfo> SnapshotTracker::get_all_snapshots() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return writer_.get_all_snapshots();
}

std::set<std::string> SnapshotTracker::pending_event_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::set<std::string> ids;
    for (const PendingEvent& pending : pending_events_) {
        ids.insert(pending.snapshot_id);
    }
    return ids;
}

std::map<std::string, uint64_t> SnapshotTracker::event_counters() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return event_counters_;
}

bool SnapshotTracker::save() {
    std::lock_guard<std::mutex> lock(mutex_);
    return writer_.save_to_file();
}

void SnapshotTracker::save_wal_locked() {
    WalState wal;

    for (const SnapshotInfo& snapshot : writer_.get_all_snapshots()) {
        if (snapshot.end_time == -1 && snapshot.trigger == MANUAL_TRIGGER) {
            wal.open_snapshots.push_back(WalOpenSnapshot{ snapshot.id, snapshot.start_time, snapshot.trigger,
                                                          snapshot.description, snapshot.tags });
        }
    }

    for (const PendingEvent& pending : pending_events_) {
        wal.pending_event_snapshots.push_back(
            WalPendingEventSnapshot{ pending.snapshot_id, pending.event_name, pending.finalize_time_ns / NS_PER_S,
                                     static_cast<int32_t>(pending.finalize_time_ns % NS_PER_S) });
    }

    writer_.save_wal(wal);
}

void SnapshotTracker::initialize_event_counters_locked() {
    // Event snapshot IDs are `event_<name>_<n>`; the name may itself contain underscores
    static const std::string EVENT_PREFIX = "event_";
    for (const SnapshotInfo& snapshot : writer_.get_all_snapshots()) {
        if (snapshot.id.rfind(EVENT_PREFIX, 0) != 0) {
            continue;
        }
        std::string suffix = snapshot.id.substr(EVENT_PREFIX.size());
        size_t last_underscore = suffix.rfind('_');
        if (last_underscore == std::string::npos) {
            continue;
        }

        try {
            uint64_t counter = std::stoull(suffix.substr(last_underscore + 1));
            uint64_t& next = event_counters_[suffix.substr(0, last_underscore)];
            next = std::max(next, counter + 1);
        } catch (const std::exception&) {
            // Not a counter: not one of ours
        }
    }
}

}  // namespace basin::log_util
