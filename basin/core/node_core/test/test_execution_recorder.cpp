#include <gtest/gtest.h>

#include <basin/node_core/determinism/execution_recorder.hpp>
#include <basin/node_core/determinism/state_archive.hpp>
#include <chrono>
#include <process_msgs/msg/dispatch_log_entry.hpp>
#include <process_msgs/msg/publish_provenance.hpp>
#include <process_msgs/msg/state_snapshot.hpp>
#include <rclcpp/rclcpp.hpp>

#include "pubsub_test_helpers.hpp"
#include "test_node.hpp"

using namespace std::chrono_literals;
using basin::node_core::determinism::EntityKind;
using basin::node_core::determinism::ExecutionRecorder;
using basin::node_core::determinism::StateArchive;
using basin::node_core::test::TestNode;
using basin::node_core::test::waitForPublishers;

namespace {
std::string determinismTopic(const rclcpp::Node& node, const std::string& leaf) {
    return std::string(node.get_fully_qualified_name()) + "/determinism/" + leaf;
}
}  // namespace

TEST(ExecutionRecorder, RecordDispatchReturnsIncreasingSequenceNumbersWithNoPublishersAttached) {
    // Sequencing doesn't depend on a node/recording being enabled -- recordDispatch is safe to
    // call (and still assigns sequence numbers) even before attachPublishers.
    ExecutionRecorder recorder;

    uint64_t s0 = recorder.recordDispatch(EntityKind::kTimer, "tick_timer", rclcpp::Time(int64_t{ 0 }, RCL_ROS_TIME));
    uint64_t s1 =
        recorder.recordDispatch(EntityKind::kSubscription, "/joint/state", rclcpp::Time(int64_t{ 1 }, RCL_ROS_TIME));
    uint64_t s2 = recorder.recordDispatch(EntityKind::kTimer, "tick_timer", rclcpp::Time(int64_t{ 2 }, RCL_ROS_TIME));

    EXPECT_EQ(s0, 0u);
    EXPECT_EQ(s1, 1u);
    EXPECT_EQ(s2, 2u);
    EXPECT_EQ(recorder.currentSequence(), s2);
}

TEST(ExecutionRecorder, TimerNameRegistryRoundTripsAndFallsBackToPlaceholder) {
    ExecutionRecorder recorder;
    rclcpp::TimerBase* fake_timer = reinterpret_cast<rclcpp::TimerBase*>(0x1234);

    // Unregistered timers still get a stable, non-empty name.
    std::string placeholder = recorder.lookupTimerName(fake_timer);
    EXPECT_FALSE(placeholder.empty());

    recorder.registerTimerName(fake_timer, "tick_timer");
    EXPECT_EQ(recorder.lookupTimerName(fake_timer), "tick_timer");
}

TEST(ExecutionRecorder, DispatchAndPublishRecordsArePublishedOnTheNodesTopics) {
    auto node = std::make_shared<TestNode>("execution_recorder_publish_test_node");
    auto recorder = node->getRecorder();  // recording is on by default for a DeterministicNode

    auto subscriber_node = std::make_shared<rclcpp::Node>("execution_recorder_publish_test_subscriber");
    std::vector<process_msgs::msg::DispatchLogEntry> dispatch_received;
    std::vector<process_msgs::msg::PublishProvenance> publish_received;
    auto dispatch_sub = subscriber_node->create_subscription<process_msgs::msg::DispatchLogEntry>(
        determinismTopic(*node, "dispatch_log"), rclcpp::QoS(100).reliable(),
        [&dispatch_received](const process_msgs::msg::DispatchLogEntry::ConstSharedPtr msg) {
            dispatch_received.push_back(*msg);
        });
    auto publish_sub = subscriber_node->create_subscription<process_msgs::msg::PublishProvenance>(
        determinismTopic(*node, "publish_log"), rclcpp::QoS(100).reliable(),
        [&publish_received](const process_msgs::msg::PublishProvenance::ConstSharedPtr msg) {
            publish_received.push_back(*msg);
        });

    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(subscriber_node);

    // The topics use volatile QoS by design (persisted only if something is listening), so the
    // subscriber has to actually be matched before publishing, or the message is just dropped.
    ASSERT_TRUE(waitForPublishers(dispatch_sub, executor, 1, 3s));
    ASSERT_TRUE(waitForPublishers(publish_sub, executor, 1, 3s));

    recorder->recordDispatch(EntityKind::kTimer, "tick_timer", rclcpp::Time(5, 0, RCL_ROS_TIME));
    rmw_gid_t gid{};
    recorder->recordPublish(0, "/state/fault_report", gid, 0);

    ASSERT_TRUE(basin::node_core::test::spinUntil(
        executor, [&]() { return dispatch_received.size() >= 1 && publish_received.size() >= 1; }, 3s));

    EXPECT_EQ(dispatch_received[0].entity_name, "tick_timer");
    EXPECT_EQ(dispatch_received[0].kind, process_msgs::msg::DispatchLogEntry::KIND_TIMER);
    EXPECT_EQ(dispatch_received[0].sim_time.sec, 5);

    EXPECT_EQ(publish_received[0].topic, "/state/fault_report");
    EXPECT_EQ(publish_received[0].producing_sequence, 0u);
}

TEST(ExecutionRecorder, SnapshotNowPublishesSavedStateImmediately) {
    rclcpp::NodeOptions options;
    options.append_parameter_override("core.state_period", 0.0);  // disable the automatic timer for this test
    auto node = std::make_shared<TestNode>("execution_recorder_snapshot_test_node", options);
    node->setCounter(99);
    auto recorder = node->getRecorder();

    auto subscriber_node = std::make_shared<rclcpp::Node>("execution_recorder_snapshot_test_subscriber");
    std::vector<process_msgs::msg::StateSnapshot> received;
    auto sub = subscriber_node->create_subscription<process_msgs::msg::StateSnapshot>(
        determinismTopic(*node, "state_snapshot"), rclcpp::QoS(100).reliable(),
        [&received](const process_msgs::msg::StateSnapshot::ConstSharedPtr msg) { received.push_back(*msg); });

    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(subscriber_node);
    ASSERT_TRUE(waitForPublishers(sub, executor, 1, 3s));

    recorder->snapshotNow(1, rclcpp::Time(int64_t{ 1 }, RCL_ROS_TIME), *node);
    EXPECT_EQ(node->saveCount(), 1);

    ASSERT_TRUE(basin::node_core::test::spinUntil(executor, [&]() { return received.size() >= 1; }, 2s));

    EXPECT_EQ(received[0].sequence, 1u);
    auto archive = basin::node_core::determinism::StateArchive{};
    archive.load(received[0].state_json);
    EXPECT_EQ(archive.get<int>("counter"), 99);
}

TEST(DeterministicNode, AutomaticSnapshotTimerPublishesPeriodicallyWithoutManualCalls) {
    rclcpp::NodeOptions options;
    options.append_parameter_override("core.state_period", 0.02);  // keep the test fast
    auto node = std::make_shared<TestNode>("deterministic_node_snapshot_timer_test_node", options);
    node->setCounter(7);

    auto subscriber_node = std::make_shared<rclcpp::Node>("deterministic_node_snapshot_timer_test_subscriber");
    std::vector<process_msgs::msg::StateSnapshot> received;
    auto sub = subscriber_node->create_subscription<process_msgs::msg::StateSnapshot>(
        determinismTopic(*node, "state_snapshot"), rclcpp::QoS(100).reliable(),
        [&received](const process_msgs::msg::StateSnapshot::ConstSharedPtr msg) { received.push_back(*msg); });

    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(subscriber_node);
    ASSERT_TRUE(waitForPublishers(sub, executor, 1, 3s));

    // Nobody calls snapshotNow() here -- the node's own snapshot timer should, on its own.
    ASSERT_TRUE(basin::node_core::test::spinUntil(executor, [&]() { return received.size() >= 2; }, 3s));

    EXPECT_EQ(node->saveCount(), static_cast<int>(received.size()));
    for (const auto& snapshot : received) {
        StateArchive archive;
        archive.load(snapshot.state_json);
        EXPECT_EQ(archive.get<int>("counter"), 7);
    }
}

TEST(DeterministicNode, SnapshotPeriodDefaultsToTwentySeconds) {
    auto node = std::make_shared<TestNode>("deterministic_node_snapshot_period_default_test_node");
    EXPECT_DOUBLE_EQ(node->get_parameter("core.state_period").as_double(), 20.0);
}
