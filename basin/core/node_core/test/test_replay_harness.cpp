#include <gtest/gtest.h>
#include <unistd.h>

#include <basin/node_core/determinism/replay_harness.hpp>
#include <filesystem>
#include <process_msgs/msg/dispatch_log_entry.hpp>
#include <process_msgs/msg/state_snapshot.hpp>
#include <rosbag2_cpp/writer.hpp>
#include <std_msgs/msg/int32.hpp>

using basin::node_core::determinism::DispatchTable;
using basin::node_core::determinism::ReplayHarness;

namespace {
// The pid keeps concurrent runs of this binary (Bazel schedules several at once, and /tmp is shared
// between them) from racing on, or reading, each other's bag directory.
std::string makeBagUri(const std::string& case_name) {
    std::string uri = (std::filesystem::temp_directory_path() /
                       ("node_core_replay_test_" + std::to_string(::getpid()) + "_" + case_name))
                          .string();
    std::filesystem::remove_all(uri);
    return uri;
}

process_msgs::msg::DispatchLogEntry makeDispatch(uint64_t sequence, uint8_t kind, const std::string& entity_name,
                                                 int32_t sim_time_sec) {
    process_msgs::msg::DispatchLogEntry entry;
    entry.sequence = sequence;
    entry.kind = kind;
    entry.entity_name = entity_name;
    entry.sim_time.sec = sim_time_sec;
    return entry;
}

process_msgs::msg::StateSnapshot makeSnapshot(uint64_t sequence, int32_t sim_time_sec, const std::string& state_json) {
    process_msgs::msg::StateSnapshot snapshot;
    snapshot.sequence = sequence;
    snapshot.sim_time.sec = sim_time_sec;
    snapshot.state_json = state_json;
    return snapshot;
}

/// Builds a synthetic bag under `<uri>` containing the same dispatch/snapshot topics
/// `DeterministicNode` would have published live, as if `ros2 bag record` had captured them.
class ReplayFixtureBag {
   public:
    explicit ReplayFixtureBag(const std::string& uri, const std::string& topic_prefix) : topic_prefix_(topic_prefix) {
        writer_.open(uri);
    }

    void writeDispatch(const process_msgs::msg::DispatchLogEntry& entry) {
        writer_.write(entry, topic_prefix_ + "/determinism/dispatch_log",
                      rclcpp::Time(static_cast<int32_t>(entry.sequence), 0, RCL_ROS_TIME));
    }

    void writeSnapshot(const process_msgs::msg::StateSnapshot& snapshot) {
        writer_.write(snapshot, topic_prefix_ + "/determinism/state_snapshot",
                      rclcpp::Time(static_cast<int32_t>(snapshot.sequence), 0, RCL_ROS_TIME));
    }

    /// Writes a message on one of the node's normal (non-determinism/*) topics, as if
    /// `ros2 bag record` captured it alongside the determinism topics -- this is what
    /// `ReplayHarness::subscriptionHandler` reads back.
    template <typename MessageT>
    void writeMessage(const std::string& topic, const MessageT& message, int32_t sim_time_sec) {
        writer_.write(message, topic, rclcpp::Time(sim_time_sec, 0, RCL_ROS_TIME));
    }

   private:
    std::string topic_prefix_;
    rosbag2_cpp::Writer writer_;
};
}  // namespace

class ReplayHarnessTest : public ::testing::Test {
   protected:
    void SetUp() override {
        bag_uri_ = makeBagUri("basic");
        topic_prefix_ = "/replay_harness_test_node";

        ReplayFixtureBag bag(bag_uri_, topic_prefix_);

        // sequence 0: tick, snapshot taken with counter == 0
        bag.writeDispatch(makeDispatch(0, process_msgs::msg::DispatchLogEntry::KIND_TIMER, "tick_timer", 0));
        bag.writeSnapshot(makeSnapshot(0, 0, R"({"counter":0})"));

        // sequence 1: subscription callback -- not every test registers a handler for this one
        bag.writeDispatch(
            makeDispatch(1, process_msgs::msg::DispatchLogEntry::KIND_SUBSCRIPTION, "/boom_joint/setpoint", 1));
        std_msgs::msg::Int32 setpoint;
        setpoint.data = 7;
        bag.writeMessage("/boom_joint/setpoint", setpoint, 1);

        // sequence 2: another tick
        bag.writeDispatch(makeDispatch(2, process_msgs::msg::DispatchLogEntry::KIND_TIMER, "tick_timer", 2));
    }  // bag's Writer destructs here, flushing everything and finalizing metadata.yaml

    void TearDown() override { std::filesystem::remove_all(bag_uri_); }

    std::string bag_uri_;
    std::string topic_prefix_;
};

TEST_F(ReplayHarnessTest, FindsNearestSnapshotAtOrBeforeTarget) {
    ReplayHarness harness(bag_uri_, topic_prefix_);

    auto snapshot = harness.findNearestSnapshot(2);
    ASSERT_TRUE(snapshot.has_value());
    EXPECT_EQ(snapshot->sequence, 0u);
}

TEST_F(ReplayHarnessTest, LoadsSnapshotStatePreviouslyPublishedByDeterministicNode) {
    ReplayHarness harness(bag_uri_, topic_prefix_);
    auto snapshot = harness.findNearestSnapshot(2);
    ASSERT_TRUE(snapshot.has_value());

    auto state = ReplayHarness::toStateArchive(*snapshot);
    EXPECT_EQ(state.get<int>("counter"), 0);
}

TEST_F(ReplayHarnessTest, ReplayForwardInvokesHandlersInOrderAndReportsUnhandled) {
    ReplayHarness harness(bag_uri_, topic_prefix_);
    auto snapshot = harness.findNearestSnapshot(2);
    ASSERT_TRUE(snapshot.has_value());

    std::vector<std::string> invoked;
    std::vector<std::string> unhandled;
    std::vector<int32_t> sim_times;

    DispatchTable table;
    table["tick_timer"] = [&invoked](const process_msgs::msg::DispatchLogEntry& e) {
        invoked.push_back(e.entity_name);
    };

    harness.replayForward(
        *snapshot, /*target_sequence=*/2, table,
        [&sim_times](rclcpp::Time t) { sim_times.push_back(static_cast<int32_t>(t.seconds())); },
        [&unhandled](const process_msgs::msg::DispatchLogEntry& e) { unhandled.push_back(e.entity_name); });

    // sequence 0 is the snapshot itself and is not replayed again; sequence 1 (subscription) has
    // no handler registered, sequence 2 (tick_timer) does.
    ASSERT_EQ(invoked.size(), 1u);
    EXPECT_EQ(invoked[0], "tick_timer");

    ASSERT_EQ(unhandled.size(), 1u);
    EXPECT_EQ(unhandled[0], "/boom_joint/setpoint");

    ASSERT_EQ(sim_times.size(), 2u);
    EXPECT_EQ(sim_times[0], 1);
    EXPECT_EQ(sim_times[1], 2);
}

TEST_F(ReplayHarnessTest, SubscriptionHandlerFeedsMessageRecordedOnTheSameTopic) {
    ReplayHarness harness(bag_uri_, topic_prefix_);
    auto snapshot = harness.findNearestSnapshot(2);
    ASSERT_TRUE(snapshot.has_value());

    std::vector<int32_t> received;
    DispatchTable table;
    table["tick_timer"] = [](const process_msgs::msg::DispatchLogEntry&) {};
    table["/boom_joint/setpoint"] = harness.subscriptionHandler<std_msgs::msg::Int32>(
        "/boom_joint/setpoint", [&received](const std_msgs::msg::Int32& msg) { received.push_back(msg.data); });

    harness.replayForward(*snapshot, /*target_sequence=*/2, table, [](rclcpp::Time) {});

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0], 7);
}
