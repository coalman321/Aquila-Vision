#include "basin/node_core/determinism/replay_harness.hpp"

namespace basin::node_core::determinism {

ReplayHarness::ReplayHarness(std::string bag_uri, std::string topic_prefix) :
    bag_uri_(std::move(bag_uri)), topic_prefix_(std::move(topic_prefix)) {}

std::shared_ptr<rosbag2_cpp::Reader> ReplayHarness::openReaderFiltered(const std::string& topic) const {
    auto reader = std::make_shared<rosbag2_cpp::Reader>();
    reader->open(bag_uri_);
    reader->set_filter(rosbag2_storage::StorageFilter{ { topic } });
    return reader;
}

std::vector<process_msgs::msg::DispatchLogEntry> ReplayHarness::loadDispatchRange(uint64_t from_sequence,
                                                                                  uint64_t to_sequence) const {
    auto reader = openReaderFiltered(dispatchTopic());

    std::vector<process_msgs::msg::DispatchLogEntry> entries;
    while (reader->has_next()) {
        auto entry = reader->read_next<process_msgs::msg::DispatchLogEntry>();
        if (entry.sequence > to_sequence) {
            // Recorded (and therefore bagged) in non-decreasing sequence order, so nothing
            // further on this topic can fall back into range.
            break;
        }
        if (entry.sequence >= from_sequence) {
            entries.push_back(std::move(entry));
        }
    }
    return entries;
}

std::optional<process_msgs::msg::StateSnapshot> ReplayHarness::findNearestSnapshot(uint64_t target_sequence) const {
    auto reader = openReaderFiltered(snapshotTopic());

    std::optional<process_msgs::msg::StateSnapshot> best;
    while (reader->has_next()) {
        auto snapshot = reader->read_next<process_msgs::msg::StateSnapshot>();
        if (snapshot.sequence > target_sequence) {
            break;  // same non-decreasing-order argument as loadDispatchRange
        }
        best = std::move(snapshot);
    }
    return best;
}

StateArchive ReplayHarness::toStateArchive(const process_msgs::msg::StateSnapshot& snapshot) {
    StateArchive archive;
    archive.load(snapshot.state_json);
    return archive;
}

void ReplayHarness::replayForward(
    const process_msgs::msg::StateSnapshot& from, uint64_t target_sequence, const DispatchTable& dispatch_table,
    const std::function<void(rclcpp::Time)>& on_sim_time,
    const std::function<void(const process_msgs::msg::DispatchLogEntry&)>& on_unhandled) const {
    auto entries = loadDispatchRange(from.sequence + 1, target_sequence);
    for (const auto& entry : entries) {
        if (on_sim_time) {
            on_sim_time(rclcpp::Time(entry.sim_time, RCL_ROS_TIME));
        }

        auto it = dispatch_table.find(entry.entity_name);
        if (it == dispatch_table.end()) {
            if (on_unhandled) {
                on_unhandled(entry);
            }
            continue;
        }
        it->second(entry);
    }
}

}  // namespace basin::node_core::determinism
