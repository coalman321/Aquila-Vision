#include <gtest/gtest.h>

#include <chrono>
#include <rclcpp/rclcpp.hpp>
#include <std_msgs/msg/int32.hpp>

#include "pubsub_test_helpers.hpp"
#include "test_node.hpp"

using namespace std::chrono_literals;
using basin::node_core::test::TestNode;

TEST(DeterministicNode, CreateDeterministicSubscriptionReceivesLiveMessages) {
    auto node = std::make_shared<TestNode>("deterministic_subscription_test_node");

    std::vector<int32_t> received;
    auto sub = node->createDeterministicSubscription<std_msgs::msg::Int32>(
        "setpoint", rclcpp::QoS(10).reliable(),
        [&received](const std_msgs::msg::Int32& msg) { received.push_back(msg.data); });

    auto publisher_node = std::make_shared<rclcpp::Node>("deterministic_subscription_test_publisher");
    auto pub =
        publisher_node->create_publisher<std_msgs::msg::Int32>(sub->get_topic_name(), rclcpp::QoS(10).reliable());

    rclcpp::executors::SingleThreadedExecutor executor;
    executor.add_node(node);
    executor.add_node(publisher_node);

    // Wait for the publisher/subscriber to actually match before publishing (volatile QoS).
    auto deadline = std::chrono::steady_clock::now() + 3s;
    while (pub->get_subscription_count() == 0 && std::chrono::steady_clock::now() < deadline) {
        executor.spin_some(20ms);
    }
    ASSERT_GT(pub->get_subscription_count(), 0u);

    std_msgs::msg::Int32 msg;
    msg.data = 42;
    pub->publish(msg);

    ASSERT_TRUE(basin::node_core::test::spinUntil(executor, [&received]() { return !received.empty(); }, 3s));

    ASSERT_EQ(received.size(), 1u);
    EXPECT_EQ(received[0], 42);
}

TEST(DeterministicNode, GetReplayTopicsTracksDeterministicSubscriptions) {
    auto node = std::make_shared<TestNode>("deterministic_subscription_topics_test_node");
    EXPECT_TRUE(node->getReplayTopics().empty());

    auto sub = node->createDeterministicSubscription<std_msgs::msg::Int32>("setpoint", rclcpp::QoS(10).reliable(),
                                                                           [](const std_msgs::msg::Int32&) {});

    ASSERT_EQ(node->getReplayTopics().size(), 1u);
    EXPECT_EQ(node->getReplayTopics()[0], sub->get_topic_name());
}
