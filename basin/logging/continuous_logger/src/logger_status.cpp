#include "continous_logger/logger_status.hpp"

#include <algorithm>

namespace basin::continuous_logger {

using logging_msgs::msg::EventStatus;
using logging_msgs::msg::SnapshotStatus;

namespace {

SnapshotStatus toStatus(const log_util::SnapshotInfo& info, uint8_t state) {
    SnapshotStatus status;
    status.id = info.id;
    status.trigger = info.trigger;
    status.description = info.description;
    status.tags = info.tags;
    status.start_time = info.start_time;
    status.end_time = info.end_time;
    status.state = state;
    return status;
}

}  // namespace

std::vector<EventStatus> makeEventStatuses(const std::vector<EventSnapshotConfig>& configs,
                                           const std::map<std::string, uint64_t>& counters) {
    std::vector<EventStatus> events;
    events.reserve(configs.size());
    for (const EventSnapshotConfig& config : configs) {
        EventStatus event;
        event.event_name = config.event_name;
        event.time_before = config.time_before;
        event.time_after = config.time_after;
        event.tags = config.tags;
        event.description = config.description;

        auto counter = counters.find(config.event_name);
        event.trigger_count = counter == counters.end() ? 0 : counter->second;

        events.push_back(event);
    }
    return events;
}

std::vector<SnapshotStatus> makeSnapshotStatuses(std::vector<log_util::SnapshotInfo> snapshots,
                                                 const std::set<std::string>& pending_ids, size_t max_completed) {
    std::stable_sort(snapshots.begin(), snapshots.end(),
                     [](const log_util::SnapshotInfo& a, const log_util::SnapshotInfo& b) {
                         return a.start_time > b.start_time;
                     });

    std::vector<SnapshotStatus> statuses;

    // A snapshot that has not been stopped has no end time yet
    for (const log_util::SnapshotInfo& info : snapshots) {
        if (info.end_time == -1) {
            statuses.push_back(toStatus(
                info, pending_ids.count(info.id) > 0 ? SnapshotStatus::STATE_PENDING : SnapshotStatus::STATE_RUNNING));
        }
    }

    size_t completed = 0;
    for (const log_util::SnapshotInfo& info : snapshots) {
        if (info.end_time != -1 && completed < max_completed) {
            statuses.push_back(toStatus(info, SnapshotStatus::STATE_COMPLETE));
            completed++;
        }
    }

    return statuses;
}

}  // namespace basin::continuous_logger
