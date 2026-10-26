#include "basin/node_core/determinism/deterministic_node.hpp"

#include <chrono>

namespace basin::node_core::determinism {

namespace {
// Deep enough that a burst of dispatches (many callbacks firing within one wake of the
// executor) doesn't drop entries before a slow subscriber (e.g. `ros2 bag record`) drains them.
rclcpp::QoS determinismQoS() { return rclcpp::QoS(1000).reliable(); }
}  // namespace

DeterministicNode::DeterministicNode(const std::string& node_name, const std::string& namespace_,
                                     const rclcpp::NodeOptions& options) :
    NodeCore(node_name, namespace_, options), recorder_(std::make_shared<ExecutionRecorder>()) {
    // `~/...` (node-private), not a bare relative name: a relative topic resolves under the
    // node's *namespace* only, not its name, so every DeterministicNode with the default empty
    // namespace would otherwise collide on the exact same `/determinism/dispatch_log` topic.
    dispatch_log_pub_ =
        create_publisher<process_msgs::msg::DispatchLogEntry>("~/determinism/dispatch_log", determinismQoS());
    publish_log_pub_ =
        create_publisher<process_msgs::msg::PublishProvenance>("~/determinism/publish_log", determinismQoS());
    state_snapshot_pub_ =
        create_publisher<process_msgs::msg::StateSnapshot>("~/determinism/state_snapshot", determinismQoS());

    recorder_->attachPublishers(dispatch_log_pub_, publish_log_pub_, state_snapshot_pub_);

    if (tick_timer_) {
        recorder_->registerTimerName(tick_timer_.get(), "tick_timer");
    }

    // Mirrors how tick_timer_'s period comes from the "core.tick_period" parameter in NodeCore:
    // the period lives in node config, not a compile-time constant, so it's overridable
    // per-node (e.g. from a launch file) without a rebuild.
    double snapshot_period{ 20.0 };
    if (!has_parameter("core.state_period")) {
        declare_parameter("core.state_period", snapshot_period);
    }
    snapshot_period = get_parameter("core.state_period").get_value<double>();
    if (snapshot_period > 0.0) {
        snapshot_timer_ = create_timer(std::chrono::duration<double>(snapshot_period), [this]() {
            recorder_->snapshotNow(recorder_->currentSequence(), get_clock()->now(), *this);
        });
        recorder_->registerTimerName(snapshot_timer_.get(), "snapshot_timer");
    }
}

void DeterministicNode::publishFaultReport(const status_msgs::msg::FaultReport& report) {
    uint64_t ordinal = fault_report_ordinal_.fetch_add(1, std::memory_order_relaxed);
    recorder_->recordPublish(recorder_->currentSequence(), fault_report_pub_->get_topic_name(),
                             fault_report_pub_->get_gid(), ordinal);
    NodeCore::publishFaultReport(report);
}

}  // namespace basin::node_core::determinism
