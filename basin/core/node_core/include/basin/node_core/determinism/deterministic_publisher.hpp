#pragma once

#include <atomic>
#include <basin/node_core/determinism/execution_recorder.hpp>
#include <functional>
#include <memory>
#include <rclcpp/publisher.hpp>
#include <string>

namespace basin::node_core::determinism {

/// @brief Wraps an `rclcpp::Publisher<MessageT>` so every `publish()` call is attributed, via
/// `ExecutionRecorder::recordPublish`, to the dispatch that produced it.
///
/// `rclcpp::Publisher::publish` isn't virtual, so a plain `rclcpp::Publisher<MessageT>::SharedPtr`
/// can't be intercepted after the fact — this type is returned by
/// `NodeCore::createDeterministicPublisher` instead. It forwards to the same publish overloads
/// callers already use, so switching a publisher over is a type change at the declaration site
/// with no change to call sites.
///
/// `recorder_provider` is called on every publish (rather than a recorder being captured once)
/// so a publisher created before `NodeCore::enableDeterministicRecording()` is called — e.g.
/// `fault_report_pub_`, created in `NodeCore`'s constructor — still records once recording is
/// enabled, instead of permanently binding a null recorder. It's a `std::function` rather than a
/// `NodeCore*` so this header has no dependency on `NodeCore`'s definition.
template <typename MessageT>
class DeterministicPublisher {
   public:
    using SharedPtr = std::shared_ptr<DeterministicPublisher<MessageT>>;
    using RecorderProvider = std::function<std::shared_ptr<ExecutionRecorder>()>;

    DeterministicPublisher(typename rclcpp::Publisher<MessageT>::SharedPtr publisher,
                           RecorderProvider recorder_provider) :
        publisher_(std::move(publisher)), recorder_provider_(std::move(recorder_provider)) {}

    void publish(const MessageT& msg) {
        recordProvenance();
        publisher_->publish(msg);
    }

    void publish(std::unique_ptr<MessageT> msg) {
        recordProvenance();
        publisher_->publish(std::move(msg));
    }

    /// @brief The wrapped `rclcpp::Publisher`, for QoS/GID introspection.
    typename rclcpp::Publisher<MessageT>::SharedPtr underlying() const { return publisher_; }

   private:
    void recordProvenance() {
        auto recorder = recorder_provider_ ? recorder_provider_() : nullptr;
        if (!recorder) {
            return;
        }
        uint64_t ordinal = ordinal_.fetch_add(1, std::memory_order_relaxed);
        recorder->recordPublish(recorder->currentSequence(), publisher_->get_topic_name(), publisher_->get_gid(),
                                ordinal);
    }

    typename rclcpp::Publisher<MessageT>::SharedPtr publisher_;
    RecorderProvider recorder_provider_;
    std::atomic<uint64_t> ordinal_{ 0 };
};

}  // namespace basin::node_core::determinism
