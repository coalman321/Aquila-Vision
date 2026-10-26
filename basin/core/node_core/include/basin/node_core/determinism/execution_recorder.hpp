#pragma once

#include <rmw/types.h>

#include <atomic>
#include <basin/node_core/determinism/types.hpp>
#include <cstdint>
#include <mutex>
#include <process_msgs/msg/dispatch_log_entry.hpp>
#include <process_msgs/msg/publish_provenance.hpp>
#include <process_msgs/msg/state_snapshot.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/timer.hpp>
#include <string>
#include <unordered_map>

namespace basin::node_core::determinism {

class DeterministicNode;

/// @brief Assigns sequence numbers for one node, and publishes dispatch/publish/snapshot
/// records through publishers created by the node it's attached to (see `DeterministicNode`'s
/// constructor).
///
/// This is deliberately *not* a file writer: `recordDispatch`/`recordPublish`/`snapshotNow` are
/// plain ROS publishes on ordinary topics. Whether any of it is actually persisted is up to
/// whether something (typically `ros2 bag record`) is listening — this class has no opinion on
/// that, and doesn't write anything to disk itself. That also means, unlike an earlier version
/// of this class, there's no async ring buffer here: the original design's async-write concern
/// was about disk I/O off the hot path, which doesn't apply to a normal `rclcpp::Publisher`
/// call — nodes publish from callbacks directly all the time.
class ExecutionRecorder {
   public:
    ExecutionRecorder() = default;

    /// @brief Wires this recorder up to the publishers `DeterministicNode` created for it.
    /// Must be called before `recordDispatch`/`recordPublish`/`snapshotNow` do anything — calls
    /// made before this are silently dropped (opt-in: a node that never enables recording never
    /// creates these publishers in the first place).
    void attachPublishers(rclcpp::Publisher<process_msgs::msg::DispatchLogEntry>::SharedPtr dispatch_pub,
                          rclcpp::Publisher<process_msgs::msg::PublishProvenance>::SharedPtr publish_pub,
                          rclcpp::Publisher<process_msgs::msg::StateSnapshot>::SharedPtr snapshot_pub);

    /// @brief rclcpp::TimerBase carries no name/topic of its own, so nodes register the name
    /// they want recorded for a given timer instance (e.g. "tick_timer", "heartbeat_timer") here.
    /// Unregistered timers are recorded under an address-derived placeholder name.
    void registerTimerName(const rclcpp::TimerBase* timer, const std::string& name);

    /// @brief Look up a name registered via `registerTimerName`, or a placeholder if none was set.
    std::string lookupTimerName(const rclcpp::TimerBase* timer) const;

    /// @brief Publish that the executor is about to dispatch `entity_name` (a `kind`) at
    /// `sim_time`, on the topic wired up by `attachPublishers`.
    /// @return The global sequence number assigned to this dispatch.
    uint64_t recordDispatch(EntityKind kind, const std::string& entity_name, rclcpp::Time sim_time);

    /// @brief Publish that a message was published on `topic` while handling dispatch
    /// `producing_sequence`. `per_writer_ordinal` is the publish count for `writer_gid` so far.
    void recordPublish(uint64_t producing_sequence, const std::string& topic, const rmw_gid_t& writer_gid,
                       uint64_t per_writer_ordinal);

    /// @brief Publish a snapshot of `node`'s state. Called by `DeterministicNode`'s own
    /// period-based snapshot timer; safe to call directly too if a node wants a snapshot on its
    /// own schedule instead (e.g. from `onTick()`).
    void snapshotNow(uint64_t sequence, rclcpp::Time sim_time, DeterministicNode& node);

    /// @brief Sequence number of the dispatch currently being handled, for use by
    /// `DeterministicPublisher` when attaching provenance to a publish call.
    uint64_t currentSequence() const { return current_sequence_.load(std::memory_order_relaxed); }

   private:
    std::atomic<uint64_t> next_sequence_{ 0 };
    std::atomic<uint64_t> current_sequence_{ 0 };

    mutable std::mutex timer_names_mutex_;
    std::unordered_map<const rclcpp::TimerBase*, std::string> timer_names_;

    rclcpp::Publisher<process_msgs::msg::DispatchLogEntry>::SharedPtr dispatch_pub_;
    rclcpp::Publisher<process_msgs::msg::PublishProvenance>::SharedPtr publish_pub_;
    rclcpp::Publisher<process_msgs::msg::StateSnapshot>::SharedPtr snapshot_pub_;
};

}  // namespace basin::node_core::determinism
