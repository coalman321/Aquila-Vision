#pragma once

#include <basin/node_core/determinism/state_archive.hpp>
#include <functional>
#include <memory>
#include <optional>
#include <process_msgs/msg/dispatch_log_entry.hpp>
#include <process_msgs/msg/state_snapshot.hpp>
#include <rclcpp/time.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_storage/storage_filter.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace basin::node_core::determinism {

/// @brief A `{name -> handler}` table a node registers so replay can invoke "the callback for
/// this dispatch" without depending on rclcpp internals (`AnySubscriptionCallback` etc., which
/// shift across distros). The same table backs both live construction-time bookkeeping and
/// replay — see the design doc, Section 5.6, point 5.
///
/// The handler is responsible for supplying whatever payload the real callback would have
/// received (e.g. by looking the message up in the same bag, on the entity's own topic) — that
/// lookup is deliberately left to the caller, since it depends on the message type, which this
/// generic harness has no way to know.
using DispatchHandler = std::function<void(const process_msgs::msg::DispatchLogEntry&)>;
using DispatchTable = std::unordered_map<std::string, DispatchHandler>;

/// @brief Reads back the `determinism/dispatch_log` and `determinism/state_snapshot` topics
/// `DeterministicNode` publishes (see its class comment) from a rosbag2 recording, and drives
/// replay from the nearest snapshot.
///
/// This is deliberately not part of `node_core` itself (per the design doc, Section 5.6) — it's
/// a small standalone engine a node-specific replay tool links against, supplying its own
/// `DispatchTable` and calling `node->applySnapshot()`/`node->get_clock()->set_ros_time_override()`
/// as steps 2-3 of the design doc's replay procedure spell out.
class ReplayHarness {
   public:
    /// @param bag_uri Path to a rosbag2 recording that captured this node's `determinism/*`
    ///        topics — i.e. something was running `ros2 bag record` against them while the node
    ///        ran with recording enabled. Nothing to replay if it wasn't.
    /// @param topic_prefix The node-relative namespace those topics were published under (e.g.
    ///        `/hex_controller` for a node with no additional namespace) — matches how
    ///        `DeterministicNode` creates them as node-private (`~/...`) topics.
    ReplayHarness(std::string bag_uri, std::string topic_prefix);

    /// @brief All dispatch log entries in `[from_sequence, to_sequence]`, in recorded order.
    std::vector<process_msgs::msg::DispatchLogEntry> loadDispatchRange(uint64_t from_sequence,
                                                                       uint64_t to_sequence) const;

    /// @brief The snapshot with the largest sequence number `<= target_sequence`, if any was
    /// recorded.
    std::optional<process_msgs::msg::StateSnapshot> findNearestSnapshot(uint64_t target_sequence) const;

    /// @brief Convenience: the `StateArchive` a snapshot's `state_json` decodes to, ready to
    /// pass to `DeterministicNode::applySnapshot`.
    static StateArchive toStateArchive(const process_msgs::msg::StateSnapshot& snapshot);

    /// @brief Walk the log forward from `from.sequence` (exclusive) to `target_sequence`
    /// (inclusive), invoking `dispatch_table[entry.entity_name]` for each entry and calling
    /// `on_sim_time(entry.sim_time)` beforehand so the caller can drive
    /// `clock->set_ros_time_override()` in lockstep with the recorded log. Entries with no
    /// matching handler are skipped (reported via `on_unhandled`, if provided).
    void replayForward(
        const process_msgs::msg::StateSnapshot& from, uint64_t target_sequence, const DispatchTable& dispatch_table,
        const std::function<void(rclcpp::Time)>& on_sim_time,
        const std::function<void(const process_msgs::msg::DispatchLogEntry&)>& on_unhandled = nullptr) const;

    /// @brief Builds the `DispatchTable` entry for a subscription registered live via
    /// `DeterministicNode::createDeterministicSubscription<MessageT>(topic, ...)`. Reads every
    /// `MessageT` recorded on `topic` in this same bag, in order, and feeds them to `callback`
    /// one per dispatch of `topic` as `replayForward` walks the log — i.e. it assumes the Nth
    /// dispatch of `topic` corresponds to the Nth message recorded on `topic`. That holds as
    /// long as whatever recorded the bag captured `topic` (e.g. via `getReplayTopics()`) with a
    /// QoS compatible enough that nothing was dropped; it isn't re-validated here.
    template <typename MessageT>
    DispatchHandler subscriptionHandler(const std::string& topic, std::function<void(const MessageT&)> callback) const {
        auto messages = std::make_shared<std::vector<MessageT>>();
        auto reader = openReaderFiltered(topic);
        while (reader->has_next()) {
            messages->push_back(reader->read_next<MessageT>());
        }

        auto index = std::make_shared<size_t>(0);
        return [messages, index, callback](const process_msgs::msg::DispatchLogEntry&) {
            if (*index >= messages->size()) {
                return;
            }
            callback((*messages)[*index]);
            ++(*index);
        };
    }

   private:
    std::string dispatchTopic() const { return topic_prefix_ + "/determinism/dispatch_log"; }
    std::string snapshotTopic() const { return topic_prefix_ + "/determinism/state_snapshot"; }
    std::shared_ptr<rosbag2_cpp::Reader> openReaderFiltered(const std::string& topic) const;

    std::string bag_uri_;
    std::string topic_prefix_;
};

}  // namespace basin::node_core::determinism
