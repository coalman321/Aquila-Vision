#include <gtest/gtest.h>

#include <basin/node_core/determinism/deterministic_executor.hpp>
#include <basin/node_core/determinism/execution_recorder.hpp>
#include <atomic>
#include <chrono>
#include <process_msgs/msg/dispatch_log_entry.hpp>
#include <rclcpp/rclcpp.hpp>
#include <thread>

#include "pubsub_test_helpers.hpp"
#include "test_node.hpp"

using namespace std::chrono_literals;
using basin::node_core::determinism::DeterministicExecutor;
using basin::node_core::test::TestNode;
using basin::node_core::test::waitForPublishers;

TEST(DeterministicExecutor, RecordsTickTimerDispatchesInOrder) {
    rclcpp::NodeOptions options;
    options.append_parameter_override("core.tick_period", 0.02);  // 20ms, keep the test fast
    auto node = std::make_shared<TestNode>("deterministic_executor_test_node", options);
    auto recorder = node->getRecorder();  // recording is on by default for a DeterministicNode

    auto subscriber_node = std::make_shared<rclcpp::Node>("deterministic_executor_test_subscriber");
    std::vector<process_msgs::msg::DispatchLogEntry> received;
    // Written by the spin thread, polled by the test thread below, hence atomic (`received` itself
    // is only read after the spin thread is joined).
    std::atomic<int> tick_messages_received{ 0 };
    auto sub = subscriber_node->create_subscription<process_msgs::msg::DispatchLogEntry>(
        std::string(node->get_fully_qualified_name()) + "/determinism/dispatch_log", rclcpp::QoS(1000).reliable(),
        [&received, &tick_messages_received](const process_msgs::msg::DispatchLogEntry::ConstSharedPtr msg) {
            received.push_back(*msg);
            if (msg->entity_name == "tick_timer") {
                ++tick_messages_received;
            }
        });

    DeterministicExecutor executor(recorder);
    executor.add_node(node);
    executor.add_node(subscriber_node);
    executor.setClock(node->get_clock());

    // Discovery has to complete on this executor before spin() takes it over, or the dispatch
    // messages published while the tick timer fires are simply dropped (volatile QoS).
    ASSERT_TRUE(waitForPublishers(sub, executor, 1, 3s));

    std::atomic<bool> spin_returned{ false };
    std::thread spin_thread([&executor, &spin_returned]() {
        executor.spin();
        spin_returned = true;
    });

    // Wait for the tick timer itself to have fired and been logged a handful of times, rather than
    // sleeping a fixed time: on a loaded machine the spin thread can take well over any fixed
    // window just to start. Not "until N messages arrive" either: rclcpp::Node also wires up its
    // own entities (e.g. `/parameter_events`) that get dispatched (and thus recorded) immediately,
    // well before the 20ms tick period elapses -- waiting for "any 2 messages" would pass before
    // the timer ever fired once.
    const auto deadline = std::chrono::steady_clock::now() + 10s;
    while (tick_messages_received.load() < 2 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }

    // `cancel()` only takes effect once `spin()` has marked itself as spinning; one issued before
    // that is silently lost and `spin()` would then run forever. Repeat it until spin() returns.
    while (!spin_returned.load()) {
        executor.cancel();
        std::this_thread::sleep_for(10ms);
    }
    spin_thread.join();
    executor.remove_node(node);
    executor.remove_node(subscriber_node);

    EXPECT_GE(node->tickCount(), 2) << "tick timer should have fired multiple times in the test window";
    ASSERT_GE(received.size(), 2u);

    // rclcpp::Node wires up its own entities (e.g. a `/parameter_events` subscription) on
    // construction, so the tick timer isn't the only thing this executor dispatches — but every
    // dispatch, regardless of which entity it names, must still be sequenced strictly increasing.
    uint64_t previous_sequence = 0;
    int tick_dispatch_count = 0;
    for (size_t i = 0; i < received.size(); ++i) {
        if (i > 0) {
            EXPECT_GT(received[i].sequence, previous_sequence);
        }
        previous_sequence = received[i].sequence;

        if (received[i].entity_name == "tick_timer") {
            EXPECT_EQ(received[i].kind, process_msgs::msg::DispatchLogEntry::KIND_TIMER);
            ++tick_dispatch_count;
        }
    }

    EXPECT_GE(tick_dispatch_count, 2) << "expected the tick timer itself to be named and dispatched repeatedly";
}
