#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <cstdlib>
#include <logging_msgs/msg/pcap_capture.hpp>
#include <memory>
#include <rclcpp/rclcpp.hpp>
#include <status_msgs/msg/fault_report.hpp>
#include <string>
#include <vector>

#include "pcap_recorder/pcap_recorder_node.hpp"
#include "pcap_recorder/pcap_stream.hpp"

using namespace std::chrono_literals;
using logging_msgs::msg::PcapCapture;
using status_msgs::msg::FaultReport;

namespace {

constexpr auto TIMEOUT = 10s;

// The fake sends a packet every 10 ms; these keep a test to a few seconds
constexpr double BUFFER_MAX_AGE_S = 0.5;
constexpr double CAPTURE_DURATION_S = 0.5;

/// Runs the real PcapRecorderNode over the fake tcpdump, with a probe node that raises faults and records what is
/// published on pcap/capture.
class PcapRecorderNodeTest : public ::testing::Test {
   protected:
    virtual std::vector<std::string> triggerFaults() const { return { "*" }; }

    void SetUp() override {
        // Bazel runs tests concurrently, so take a DDS domain of our own (see the logger's node tests)
        rclcpp::InitOptions init_options;
        init_options.set_domain_id(static_cast<size_t>(::getpid()) % 102);
        rclcpp::init(0, nullptr, init_options);

        rclcpp::NodeOptions options;
        options.append_parameter_override("core.tick_period", 0.05);
        options.append_parameter_override("tcpdump.path", std::string("../") + std::getenv("FAKE_TCPDUMP"));
        options.append_parameter_override("tcpdump.interface", std::string("fake0"));
        options.append_parameter_override("buffer.max_age", BUFFER_MAX_AGE_S);
        options.append_parameter_override("capture.duration", CAPTURE_DURATION_S);
        // Small enough that one capture spans several messages
        options.append_parameter_override("capture.max_message_bytes", int64_t{ 512 });
        options.append_parameter_override("trigger.faults", triggerFaults());
        recorder_ = std::make_shared<basin::pcap_recorder::PcapRecorderNode>(options);

        probe_ = std::make_shared<rclcpp::Node>("pcap_recorder_probe");
        fault_pub_ = probe_->create_publisher<FaultReport>("/state/fault_report", rclcpp::QoS(20).reliable());
        capture_sub_ = probe_->create_subscription<PcapCapture>(
            "pcap/capture", rclcpp::QoS(100).reliable(),
            [this](PcapCapture::ConstSharedPtr msg) { captures_.push_back(*msg); });

        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_node(recorder_);
        executor_->add_node(probe_);

        spinUntil([this] { return fault_pub_->get_subscription_count() > 0 && capture_sub_->get_publisher_count() > 0; });
    }

    void TearDown() override {
        capture_sub_.reset();
        fault_pub_.reset();
        executor_->remove_node(probe_);
        executor_->remove_node(recorder_);
        executor_.reset();
        probe_.reset();
        recorder_.reset();
        rclcpp::shutdown();
    }

    template <typename Predicate>
    void spinUntil(Predicate done) {
        const auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
        while (std::chrono::steady_clock::now() < deadline) {
            executor_->spin_some(20ms);
            if (done()) {
                return;
            }
        }
        FAIL() << "condition not met within the timeout";
    }

    void spinFor(std::chrono::milliseconds duration) {
        const auto deadline = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < deadline) {
            executor_->spin_some(20ms);
        }
    }

    /// Publishes a fault as NodeCore::sendFaultReport would, raised at `raise_time` and valid for a second from now.
    void raiseFault(const std::string& system, const std::string& name, const rclcpp::Time& raise_time) {
        FaultReport report;
        report.key.fault_system = system;
        report.key.fault_name = name;
        report.source = "/some_node";
        report.raise_time = raise_time;
        report.expiration_time = probe_->now() + rclcpp::Duration(1s);
        fault_pub_->publish(report);
    }

    bool lastReceived() const { return !captures_.empty() && captures_.back().is_last; }

    /// The fake's payload counters, in order, across every message received.
    std::vector<uint64_t> counters() const {
        std::vector<uint64_t> out;
        for (const PcapCapture& msg : captures_) {
            basin::pcap_recorder::PcapStreamParser parser;
            parser.feed(msg.data.data(), msg.data.size(), [&](basin::pcap_recorder::PcapPacketPtr packet) {
                uint64_t counter = 0;
                for (std::size_t i = basin::pcap_recorder::PCAP_RECORD_HEADER_SIZE; i < packet->record.size(); ++i) {
                    counter = (counter << 8) | packet->record[i];
                }
                out.push_back(counter);
            });
        }
        return out;
    }

    std::shared_ptr<basin::pcap_recorder::PcapRecorderNode> recorder_;
    std::shared_ptr<rclcpp::Node> probe_;
    rclcpp::Publisher<FaultReport>::SharedPtr fault_pub_;
    rclcpp::Subscription<PcapCapture>::SharedPtr capture_sub_;
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
    std::vector<PcapCapture> captures_;
};

TEST_F(PcapRecorderNodeTest, PublishesNothingUntilAFault) {
    spinFor(1000ms);
    EXPECT_TRUE(captures_.empty());
}

TEST_F(PcapRecorderNodeTest, PublishesTheBufferThenLiveTrafficThenStops) {
    // Run long enough that the buffer is full and has dropped its oldest packets
    spinFor(1500ms);

    const rclcpp::Time trigger_time = probe_->now();
    raiseFault("drive", "motor_overcurrent", trigger_time);
    spinUntil([this] { return lastReceived(); });

    ASSERT_GE(captures_.size(), 3u);
    uint64_t packets = 0;
    for (std::size_t i = 0; i < captures_.size(); ++i) {
        const PcapCapture& msg = captures_[i];
        EXPECT_EQ(msg.capture_id, captures_.front().capture_id);
        EXPECT_EQ(msg.sequence, i);
        EXPECT_EQ(msg.is_last, i + 1 == captures_.size());
        EXPECT_EQ(msg.trigger, "drive/motor_overcurrent");
        EXPECT_EQ(msg.interface, "fake0");
        EXPECT_LE(msg.data.size(), 512u);
        packets += msg.packet_count;
    }

    // Every packet once, in order, with no gaps
    const std::vector<uint64_t> seen = counters();
    ASSERT_EQ(seen.size(), packets);
    for (std::size_t i = 1; i < seen.size(); ++i) {
        ASSERT_EQ(seen[i], seen[i - 1] + 1) << "at packet " << i;
    }

    // It starts about max_age before the trigger -- the 1.5 s of earlier traffic has aged out -- and runs to
    // about capture.duration after it
    const rclcpp::Time first(captures_.front().first_packet_time);
    const rclcpp::Time last(captures_.back().packet_count > 0 ? captures_.back().last_packet_time
                                                              : captures_[captures_.size() - 2].last_packet_time);
    const double before = (trigger_time - first).seconds();
    const double after = (last - trigger_time).seconds();
    EXPECT_GT(before, BUFFER_MAX_AGE_S - 0.2);
    EXPECT_LT(before, BUFFER_MAX_AGE_S + 0.2);
    EXPECT_GT(after, CAPTURE_DURATION_S - 0.2);
    EXPECT_LT(after, CAPTURE_DURATION_S + 0.5);

    // And then nothing more
    const std::size_t count = captures_.size();
    spinFor(500ms);
    EXPECT_EQ(captures_.size(), count);
}

TEST_F(PcapRecorderNodeTest, ARepeatedReportOfTheSameFaultDoesNotRetrigger) {
    spinFor(300ms);
    const rclcpp::Time raise_time = probe_->now();
    raiseFault("drive", "motor_overcurrent", raise_time);
    spinUntil([this] { return lastReceived(); });
    const std::string first_capture = captures_.front().capture_id;

    // Still the same period: ignored
    captures_.clear();
    raiseFault("drive", "motor_overcurrent", raise_time);
    spinFor(1000ms);
    EXPECT_TRUE(captures_.empty());

    // Raised again: a new capture
    raiseFault("drive", "motor_overcurrent", probe_->now());
    spinUntil([this] { return lastReceived(); });
    EXPECT_NE(captures_.front().capture_id, first_capture);
    EXPECT_EQ(captures_.front().sequence, 0u);
}

TEST_F(PcapRecorderNodeTest, AnExpiredReportDoesNotTrigger) {
    spinFor(300ms);
    FaultReport report;
    report.key.fault_system = "drive";
    report.key.fault_name = "old";
    report.raise_time = probe_->now() - rclcpp::Duration(10s);
    report.expiration_time = probe_->now() - rclcpp::Duration(5s);
    fault_pub_->publish(report);
    spinFor(1000ms);
    EXPECT_TRUE(captures_.empty());
}

class PcapRecorderNodeFilterTest : public PcapRecorderNodeTest {
   protected:
    std::vector<std::string> triggerFaults() const override { return { "drive/*", "power/battery_low" }; }
};

TEST_F(PcapRecorderNodeFilterTest, OnlyMatchingFaultsTrigger) {
    spinFor(300ms);
    raiseFault("power", "brownout", probe_->now());
    raiseFault("comms", "heartbeat_timeout", probe_->now());
    spinFor(1000ms);
    EXPECT_TRUE(captures_.empty());

    raiseFault("power", "battery_low", probe_->now());
    spinUntil([this] { return lastReceived(); });
    EXPECT_EQ(captures_.front().trigger, "power/battery_low");
}

}  // namespace
