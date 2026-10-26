#include "basin/node_core/determinism/execution_recorder.hpp"

#include <algorithm>
#include <basin/node_core/determinism/deterministic_node.hpp>
#include <basin/node_core/determinism/state_archive.hpp>
#include <sstream>

namespace basin::node_core::determinism {

namespace {
uint8_t entityKindToMsg(EntityKind kind) {
    switch (kind) {
        case EntityKind::kTimer:
            return process_msgs::msg::DispatchLogEntry::KIND_TIMER;
        case EntityKind::kSubscription:
            return process_msgs::msg::DispatchLogEntry::KIND_SUBSCRIPTION;
        case EntityKind::kService:
            return process_msgs::msg::DispatchLogEntry::KIND_SERVICE;
        case EntityKind::kClient:
            return process_msgs::msg::DispatchLogEntry::KIND_CLIENT;
        case EntityKind::kWaitable:
            return process_msgs::msg::DispatchLogEntry::KIND_WAITABLE;
    }
    return process_msgs::msg::DispatchLogEntry::KIND_WAITABLE;
}
}  // namespace

void ExecutionRecorder::attachPublishers(rclcpp::Publisher<process_msgs::msg::DispatchLogEntry>::SharedPtr dispatch_pub,
                                         rclcpp::Publisher<process_msgs::msg::PublishProvenance>::SharedPtr publish_pub,
                                         rclcpp::Publisher<process_msgs::msg::StateSnapshot>::SharedPtr snapshot_pub) {
    dispatch_pub_ = std::move(dispatch_pub);
    publish_pub_ = std::move(publish_pub);
    snapshot_pub_ = std::move(snapshot_pub);
}

void ExecutionRecorder::registerTimerName(const rclcpp::TimerBase* timer, const std::string& name) {
    std::lock_guard<std::mutex> lock(timer_names_mutex_);
    timer_names_[timer] = name;
}

std::string ExecutionRecorder::lookupTimerName(const rclcpp::TimerBase* timer) const {
    std::lock_guard<std::mutex> lock(timer_names_mutex_);
    auto it = timer_names_.find(timer);
    if (it != timer_names_.end()) {
        return it->second;
    }
    std::ostringstream oss;
    oss << "timer_0x" << std::hex << reinterpret_cast<uintptr_t>(timer);
    return oss.str();
}

uint64_t ExecutionRecorder::recordDispatch(EntityKind kind, const std::string& entity_name, rclcpp::Time sim_time) {
    uint64_t sequence = next_sequence_.fetch_add(1, std::memory_order_relaxed);
    current_sequence_.store(sequence, std::memory_order_relaxed);

    if (dispatch_pub_) {
        process_msgs::msg::DispatchLogEntry msg;
        msg.sequence = sequence;
        msg.kind = entityKindToMsg(kind);
        msg.entity_name = entity_name;
        msg.sim_time = sim_time;
        dispatch_pub_->publish(msg);
    }

    return sequence;
}

void ExecutionRecorder::recordPublish(uint64_t producing_sequence, const std::string& topic,
                                      const rmw_gid_t& writer_gid, uint64_t per_writer_ordinal) {
    if (!publish_pub_) {
        return;
    }

    process_msgs::msg::PublishProvenance msg;
    msg.producing_sequence = producing_sequence;
    msg.topic = topic;
    std::copy(std::begin(writer_gid.data), std::end(writer_gid.data), msg.writer_gid.begin());
    msg.per_writer_ordinal = per_writer_ordinal;
    publish_pub_->publish(msg);
}

void ExecutionRecorder::snapshotNow(uint64_t sequence, rclcpp::Time sim_time, DeterministicNode& node) {
    if (!snapshot_pub_) {
        return;
    }

    StateArchive archive;
    node.saveState(archive);

    process_msgs::msg::StateSnapshot msg;
    msg.sequence = sequence;
    msg.sim_time = sim_time;
    msg.state_json = archive.dump();
    snapshot_pub_->publish(msg);
}

}  // namespace basin::node_core::determinism
