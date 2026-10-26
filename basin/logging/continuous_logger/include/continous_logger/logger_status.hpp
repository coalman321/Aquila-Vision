#pragma once

#include <basin/log_util/snapshot_writer.hpp>
#include <cstddef>
#include <cstdint>
#include <logging_msgs/msg/event_status.hpp>
#include <logging_msgs/msg/snapshot_status.hpp>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "continous_logger/logger_node_params.hpp"

namespace basin::continuous_logger {

/// Most completed snapshots a status message lists; snapshots persist across runs, so the list would
/// otherwise grow without bound.
constexpr size_t MAX_STATUS_COMPLETED_SNAPSHOTS = 50;

/// @brief Describes each configured event snapshot trigger
/// @param configs The event snapshot configurations
/// @param counters Per-event-name snapshot counters; an event missing from it has not triggered yet
std::vector<logging_msgs::msg::EventStatus> makeEventStatuses(const std::vector<EventSnapshotConfig>& configs,
                                                              const std::map<std::string, uint64_t>& counters);

/// @brief Describes snapshots for a status message: every one that is running or pending, then up to
/// `max_completed` of the most recently started completed ones. Each group is newest first.
/// @param snapshots All snapshots the logger knows
/// @param pending_ids IDs of the event snapshots still inside their time_after window
std::vector<logging_msgs::msg::SnapshotStatus> makeSnapshotStatuses(
    std::vector<log_util::SnapshotInfo> snapshots, const std::set<std::string>& pending_ids,
    size_t max_completed = MAX_STATUS_COMPLETED_SNAPSHOTS);

}  // namespace basin::continuous_logger
