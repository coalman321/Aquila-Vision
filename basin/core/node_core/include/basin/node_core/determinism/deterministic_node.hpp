#pragma once

#include <atomic>
#include <basin/node_core/determinism/deterministic_publisher.hpp>
#include <basin/node_core/determinism/execution_recorder.hpp>
#include <basin/node_core/determinism/state_archive.hpp>
#include <basin/node_core/node_core.hpp>
#include <functional>
#include <memory>
#include <process_msgs/msg/dispatch_log_entry.hpp>
#include <process_msgs/msg/publish_provenance.hpp>
#include <process_msgs/msg/state_snapshot.hpp>
#include <rclcpp/subscription.hpp>
#include <rclcpp/timer.hpp>
#include <status_msgs/msg/fault_report.hpp>
#include <string>
#include <vector>

namespace basin::node_core::determinism {

/// @brief Opt-in extension of `NodeCore` for nodes that participate in deterministic
/// recording/replay (see `deterministic.md`). A plain `NodeCore` subclass that never derives
/// from this is completely unaffected — the recording surface lives here rather than on
/// `NodeCore` itself, so it's additive by construction rather than by convention.
///
/// Deriving from `DeterministicNode` is the opt-in: recording is on by default from
/// construction, not a second step a caller can forget. It creates its own `ExecutionRecorder`
/// and `~/determinism/*` publishers in its constructor and hands the recorder to whatever
/// `DeterministicExecutor` spins it via `getRecorder()`.
///
/// Recording is just publishing: `sequence`/`entity_name`/`sim_time` and state snapshots go out
/// as ordinary messages on ordinary topics (`process_msgs/msg/DispatchLogEntry`,
/// `PublishProvenance`, `StateSnapshot`). Nothing is written to disk by this node_core — the log
/// only becomes durable if something is listening (typically `ros2 bag record` capturing this
/// node's `determinism/*` topics alongside its normal ones). If nothing is listening, the
/// publishes are simply discarded, same as any other unread topic; that's expected, not an
/// error condition.
class DeterministicNode : public NodeCore {
   public:
    /// This constructor always creates a dedicated snapshot timer, the same way `NodeCore`
    /// always creates `tick_timer_`: its period comes from the `core.state_period` node
    /// parameter (declared here, so it's overridable per-node the same way `core.tick_period`
    /// is), defaulting to 20 seconds. Every time it fires it calls
    /// `ExecutionRecorder::snapshotNow` directly — periodic snapshotting is automatic, not
    /// something the node has to remember to trigger. Set `core.state_period` to 0 (e.g. via a
    /// launch-file parameter override) to disable it for a node type with no meaningful state
    /// to checkpoint.
    DeterministicNode(const std::string& node_name, const std::string& namespace_ = "",
                      const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

    /// @brief The recorder created at construction. Pass this to the `DeterministicExecutor`
    /// this node will be spun with.
    std::shared_ptr<ExecutionRecorder> getRecorder() const { return recorder_; }

    /// @brief Use in place of `create_publisher<T>(...)` for any publisher whose output needs to
    /// participate in deterministic recording/replay (i.e. its output affects node behavior, as
    /// opposed to pure liveness signaling like heartbeats — see README for the heartbeat
    /// scoping decision).
    template <typename MessageT>
    typename DeterministicPublisher<MessageT>::SharedPtr createDeterministicPublisher(const std::string& topic,
                                                                                      const rclcpp::QoS& qos) {
        auto publisher = create_publisher<MessageT>(topic, qos);
        return std::make_shared<DeterministicPublisher<MessageT>>(std::move(publisher), [this]() { return recorder_; });
    }

    /// @brief Use in place of `create_subscription<T>(...)` for any subscription whose callback
    /// affects node behavior. Unlike `createDeterministicPublisher`, this doesn't need to wrap
    /// anything for *recording* — `DeterministicExecutor` already records every dispatch,
    /// subscriptions included, by topic name, at the executor level, regardless of how the
    /// subscription was created. What this buys you:
    ///  - the topic gets tracked in `getReplayTopics()`, so a `ros2 bag record` invocation
    ///    covering everything a replay needs can be generated instead of hand-maintained, and
    ///  - the callback is normalized to `void(const MessageT&)`, which is exactly the shape
    ///    `ReplayHarness::subscriptionHandler<MessageT>` needs to drive it during replay from
    ///    messages read back out of the bag on this same topic.
    template <typename MessageT>
    typename rclcpp::Subscription<MessageT>::SharedPtr createDeterministicSubscription(
        const std::string& topic, const rclcpp::QoS& qos, std::function<void(const MessageT&)> callback) {
        auto subscription = create_subscription<MessageT>(
            topic, qos, [callback](const MessageT& msg) { callback(msg); });
        replay_topics_.push_back(subscription->get_topic_name());
        return subscription;
    }

    /// @brief Topics — beyond `~/determinism/*` — a `ros2 bag record` needs to capture for this
    /// node's recording to be replayable: every topic subscribed to via
    /// `createDeterministicSubscription`.
    const std::vector<std::string>& getReplayTopics() const { return replay_topics_; }

    /// @brief Replay entry point: restore state written by `saveState` at a snapshot. Public
    /// (unlike `restoreState` itself) because the replay harness (design doc Section 5.6)
    /// constructs the node and calls this before walking the log forward — it isn't a
    /// `DeterministicNode` subclass, so it can't reach the protected hook directly.
    void applySnapshot(const StateArchive& archive) { restoreState(archive); }

   protected:
    /// @brief Serialize whatever mutable state affects future onTick()/callback output. Called
    /// whenever a snapshot is taken (the automatic timer, or a direct `snapshotNow()` call). A
    /// concrete node must implement this even if it has no meaningful state to checkpoint (an
    /// empty body is fine) — there's no default no-op, so it isn't possible to forget this
    /// silently.
    virtual void saveState(StateArchive& archive) = 0;

    /// @brief Restore state previously written by `saveState`, as the first step of replay from
    /// a snapshot. Must mirror `saveState` field-for-field.
    virtual void restoreState(const StateArchive& archive) = 0;

    /// @brief Routes fault reports through the recorder (they're node-logic output, unlike
    /// heartbeats) instead of NodeCore's plain publish.
    void publishFaultReport(const status_msgs::msg::FaultReport& report) override;

   private:
    friend class ExecutionRecorder;  ///< calls the protected saveState() on snapshot

    std::shared_ptr<ExecutionRecorder> recorder_;
    std::atomic<uint64_t> fault_report_ordinal_{ 0 };
    std::vector<std::string> replay_topics_;

    rclcpp::Publisher<process_msgs::msg::DispatchLogEntry>::SharedPtr dispatch_log_pub_;
    rclcpp::Publisher<process_msgs::msg::PublishProvenance>::SharedPtr publish_log_pub_;
    rclcpp::Publisher<process_msgs::msg::StateSnapshot>::SharedPtr state_snapshot_pub_;

    /// Null only if the `core.state_period` parameter was set to 0 — see the constructor doc.
    rclcpp::TimerBase::SharedPtr snapshot_timer_;
};

}  // namespace basin::node_core::determinism
