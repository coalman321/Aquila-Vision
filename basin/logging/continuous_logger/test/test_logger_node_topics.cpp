#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <logging_msgs/msg/log_metadata.hpp>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <string>

#include "continous_logger/logger_node.hpp"

using namespace std::chrono_literals;
using logging_msgs::msg::LogMetadata;

namespace {

constexpr auto TIMEOUT = 10s;

/// Runs the real LoggerNode next to a probe node that publishes topics, and checks which of them the logger
/// subscribes to: a named topic, and every topic matching a pattern, including ones that appear later.
class LoggerNodeTopicsTest : public ::testing::Test {
   protected:
    void SetUp() override {
        // Bazel runs tests concurrently, and processes that share a DDS domain interfere with each other
        // (see runner.cpp in //basin/core/node_core), so take a domain of our own.
        rclcpp::InitOptions init_options;
        init_options.set_domain_id(static_cast<size_t>(::getpid()) % 102);
        rclcpp::init(0, nullptr, init_options);

        log_dir_ = std::filesystem::temp_directory_path() / ("logger_node_topics_test_" + std::to_string(::getpid()));

        rclcpp::NodeOptions options;
        options.append_parameter_override("log_path", log_dir_.string());
        options.append_parameter_override("topics.named.name", std::string("/named"));
        options.append_parameter_override("topics.named.throttle", 0);
        options.append_parameter_override("topics.cameras.name", std::string("/*/driver/camera_info"));
        options.append_parameter_override("topics.cameras.throttle", 0);
        logger_ = std::make_shared<basin::continuous_logger::LoggerNode>(options);

        probe_ = std::make_shared<rclcpp::Node>("logger_topics_probe");

        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_node(logger_);
        executor_->add_node(probe_);
    }

    void TearDown() override {
        executor_->remove_node(probe_);
        executor_->remove_node(logger_);
        executor_.reset();
        probe_.reset();
        logger_.reset();
        rclcpp::shutdown();
        std::error_code ignored;
        std::filesystem::remove_all(log_dir_, ignored);
    }

    rclcpp::Publisher<LogMetadata>::SharedPtr publish(const std::string& topic) {
        return probe_->create_publisher<LogMetadata>(topic, rclcpp::SystemDefaultsQoS());
    }

    /// Spins until the logger subscribes to `topic`; returns whether it did within the timeout.
    bool spinUntilSubscribed(const std::string& topic) {
        auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
        while (std::chrono::steady_clock::now() < deadline) {
            executor_->spin_some(50ms);
            if (probe_->count_subscribers(topic) > 0) {
                return true;
            }
        }
        return false;
    }

    /// Spins for `duration` regardless of what arrives.
    void spinFor(std::chrono::milliseconds duration) {
        auto deadline = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < deadline) {
            executor_->spin_some(50ms);
        }
    }

    std::filesystem::path log_dir_;
    std::shared_ptr<basin::continuous_logger::LoggerNode> logger_;
    std::shared_ptr<rclcpp::Node> probe_;
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
};

TEST_F(LoggerNodeTopicsTest, SubscribesToANamedTopic) {
    auto pub = publish("/named");
    EXPECT_TRUE(spinUntilSubscribed("/named"));
}

TEST_F(LoggerNodeTopicsTest, SubscribesToEveryTopicMatchingAPattern) {
    auto front = publish("/front/driver/camera_info");
    auto back = publish("/back/driver/camera_info");
    EXPECT_TRUE(spinUntilSubscribed("/front/driver/camera_info"));
    EXPECT_TRUE(spinUntilSubscribed("/back/driver/camera_info"));
}

TEST_F(LoggerNodeTopicsTest, SubscribesToAMatchingTopicThatAppearsLater) {
    auto named = publish("/named");
    auto front = publish("/front/driver/camera_info");
    ASSERT_TRUE(spinUntilSubscribed("/named"));
    ASSERT_TRUE(spinUntilSubscribed("/front/driver/camera_info"));

    // Every topic present so far is subscribed, which used to end discovery
    spinFor(2s);

    auto side = publish("/side/driver/camera_info");
    EXPECT_TRUE(spinUntilSubscribed("/side/driver/camera_info"));
}

TEST_F(LoggerNodeTopicsTest, PatternMatchesOneSegmentPerStar) {
    auto nested = publish("/a/b/driver/camera_info");
    auto bare = publish("/driver/camera_info");
    auto front = publish("/front/driver/camera_info");
    ASSERT_TRUE(spinUntilSubscribed("/front/driver/camera_info"));

    // The logger has seen all three by the time it subscribed to the one that matches; give it a few more passes
    spinFor(2s);
    EXPECT_EQ(probe_->count_subscribers("/a/b/driver/camera_info"), 0u);
    EXPECT_EQ(probe_->count_subscribers("/driver/camera_info"), 0u);
}

}  // namespace
