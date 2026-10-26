#include "basin/node_core/node_monitor.hpp"

#include <cinttypes>
#include <functional>

namespace basin::monitor_core {
HeartbeatSender::HeartbeatSender(rclcpp::Node* node) {
    clock_ptr_ = node->get_node_clock_interface();
    logger_ptr_ = node->get_node_logging_interface();
    beat_pub_ = node->create_publisher<process_msgs::msg::ProcessHeartbeat>("/state/process_heartbeat",
                                                                            rclcpp::QoS(10).reliable());

    beat_timer_ = node->create_timer(500ms, [this]() -> void { sendBeat(ProcessState::RUNNING); });
    beat_timer_->cancel();

    qualified_name_ = node->get_fully_qualified_name();

    sendBeat(ProcessState::STARTING);

    // call wait_for_all_acked before shutdown
    using rclcpp::contexts::get_global_default_context;
    context_ = get_global_default_context();
    shutdown_handle_ = context_->add_pre_shutdown_callback([this]() { this->sendShutdownBeat(); });
}

HeartbeatSender::~HeartbeatSender() { context_->remove_pre_shutdown_callback(shutdown_handle_); }

void HeartbeatSender::startBeatTimer() { beat_timer_->reset(); }

void HeartbeatSender::sendBeat(ProcessState state) {
    process_msgs::msg::ProcessHeartbeat beat;
    beat.header.stamp = clock_ptr_->get_clock()->now();
    beat.mode = static_cast<uint8_t>(state);
    beat.process_name = qualified_name_;

    beat_pub_->publish(beat);
}

void HeartbeatSender::sendShutdownBeat() {
    // Cancel the beat timer
    beat_timer_->cancel();

    // make sure we sent out a shutdown beat
    sendBeat(ProcessState::HALTING);

    // Confirm all subscribers receive sent messages.
    // Note that if no subscription is connected, wait_for_all_acked() always return true.
    if (beat_pub_->wait_for_all_acked(pub_timeout_)) {
        RCLCPP_INFO(logger_ptr_->get_logger(), "All subscribers acknowledge messages");
    } else {
        RCLCPP_INFO(logger_ptr_->get_logger(), "Not all subscribers acknowledge messages during %" PRId64 " ms",
                    static_cast<int64_t>(pub_timeout_.count()));
    }
}

}  // namespace basin::monitor_core
