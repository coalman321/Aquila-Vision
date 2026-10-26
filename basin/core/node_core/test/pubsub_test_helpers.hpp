#pragma once

#include <chrono>
#include <functional>
#include <rclcpp/executor.hpp>

namespace basin::node_core::test {

/// @brief Spins `executor` until `predicate()` is true or `timeout` elapses.
/// @return Whether `predicate()` became true before the timeout.
inline bool spinUntil(rclcpp::Executor& executor, const std::function<bool()>& predicate,
                      std::chrono::milliseconds timeout) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!predicate()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        executor.spin_some(std::chrono::milliseconds(20));
    }
    return true;
}

/// @brief Waits for DDS discovery to match `subscription` with at least `count` publisher(s).
/// The recording topics use volatile (non-transient-local) QoS by design — matching "persisted
/// only if something is listening" — so tests must wait for the match before publishing, or the
/// message is simply dropped, same as it would be live.
template <typename SubscriptionT>
bool waitForPublishers(const SubscriptionT& subscription, rclcpp::Executor& executor, size_t count,
                       std::chrono::milliseconds timeout) {
    return spinUntil(
        executor, [&subscription, count]() { return subscription->get_publisher_count() >= count; }, timeout);
}

}  // namespace basin::node_core::test
