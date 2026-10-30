#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <functional>
#include <logging_msgs/msg/logger_status.hpp>
#include <logging_msgs/srv/snapshot_request.hpp>
#include <memory>
#include <optional>
#include <rclcpp/rclcpp.hpp>
#include <status_msgs/msg/fault_report.hpp>
#include <string>
#include <vector>

#include "continous_logger/logger_node.hpp"

using namespace std::chrono_literals;
using logging_msgs::msg::LoggerStatus;
using logging_msgs::msg::SnapshotStatus;
using logging_msgs::srv::SnapshotRequest;
using status_msgs::msg::FaultReport;

namespace {

constexpr auto TIMEOUT = 10s;

/// Runs the real LoggerNode next to a probe node, and checks what reaches a subscriber of
/// /log/logger_status as the logger's snapshots change, and of /state/fault_report.
class LoggerNodeStatusTest : public ::testing::Test {
   protected:
    /// The snapshot usage threshold the logger runs with. How much of the disk snapshots fill is up to the machine
    /// the test runs on, so the fault tests pick a threshold no usage can be under (0) or over (100).
    virtual double snapshotThresholdPercent() const { return 85.0; }

    /// How often the logger checks the disk, which is how often it reports the disk fault. The default (60 s) is
    /// longer than a test can wait.
    static constexpr double DISK_CHECK_INTERVAL_S = 0.2;

    void SetUp() override {
        // Bazel runs tests concurrently, and processes that share a DDS domain interfere with each other
        // (see runner.cpp in //basin/core/node_core), so take a domain of our own.
        rclcpp::InitOptions init_options;
        init_options.set_domain_id(static_cast<size_t>(::getpid()) % 102);
        rclcpp::init(0, nullptr, init_options);

        log_dir_ = std::filesystem::temp_directory_path() / ("logger_node_status_test_" + std::to_string(::getpid()));

        rclcpp::NodeOptions options;
        options.append_parameter_override("log_path", log_dir_.string());
        options.append_parameter_override("rotation_threshold_percent", 91.0);
        options.append_parameter_override("disk_check_interval", DISK_CHECK_INTERVAL_S);
        options.append_parameter_override("snapshot_usage_threshold_percent", snapshotThresholdPercent());
        options.append_parameter_override("event_snapshots.crash.event_name", std::string("crash"));
        options.append_parameter_override("event_snapshots.crash.time_before", 4.0);
        options.append_parameter_override("event_snapshots.crash.time_after", 1.5);
        options.append_parameter_override("event_snapshots.crash.tags", std::vector<std::string>{ "a", "b" });
        options.append_parameter_override("event_snapshots.crash.description", std::string("a crash"));
        logger_ = std::make_shared<basin::continuous_logger::LoggerNode>(options);

        probe_ = std::make_shared<rclcpp::Node>("logger_status_probe");
        status_sub_ = probe_->create_subscription<LoggerStatus>(
            "/log/logger_status", rclcpp::QoS(1).reliable().transient_local(),
            [this](LoggerStatus::ConstSharedPtr msg) { status_ = *msg; });
        fault_sub_ = probe_->create_subscription<FaultReport>(
            "/state/fault_report", rclcpp::QoS(20).reliable().transient_local(),
            [this](FaultReport::ConstSharedPtr msg) { faults_.push_back(*msg); });
        client_ = probe_->create_client<SnapshotRequest>("log/snapshot");

        executor_ = std::make_unique<rclcpp::executors::SingleThreadedExecutor>();
        executor_->add_node(logger_);
        executor_->add_node(probe_);
    }

    void TearDown() override {
        status_sub_.reset();
        fault_sub_.reset();
        client_.reset();
        executor_->remove_node(probe_);
        executor_->remove_node(logger_);
        executor_.reset();
        probe_.reset();
        logger_.reset();
        rclcpp::shutdown();
        std::error_code ignored;
        std::filesystem::remove_all(log_dir_, ignored);
    }

    /// Spins until the latest status satisfies `done`; fails the test if it never does.
    void spinUntilStatus(const std::function<bool(const LoggerStatus&)>& done) {
        auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
        while (std::chrono::steady_clock::now() < deadline) {
            executor_->spin_some(50ms);
            if (status_.has_value() && done(*status_)) {
                return;
            }
        }
        FAIL() << "no status satisfied the condition within the timeout";
    }

    /// Spins for `duration` regardless of what arrives.
    void spinFor(std::chrono::milliseconds duration) {
        auto deadline = std::chrono::steady_clock::now() + duration;
        while (std::chrono::steady_clock::now() < deadline) {
            executor_->spin_some(50ms);
        }
    }

    const FaultReport* findFault(const std::string& fault_name) const {
        for (const FaultReport& fault : faults_) {
            if (fault.key.fault_name == fault_name) {
                return &fault;
            }
        }
        return nullptr;
    }

    void callSnapshotService(uint8_t request_id, const std::string& name) {
        ASSERT_TRUE(client_->wait_for_service(TIMEOUT));
        auto request = std::make_shared<SnapshotRequest::Request>();
        request->request_id = request_id;
        request->snapshot_name = name;
        request->description = "made by the test";
        request->tags = { "t" };
        auto future = client_->async_send_request(request);
        ASSERT_EQ(executor_->spin_until_future_complete(future, TIMEOUT), rclcpp::FutureReturnCode::SUCCESS);
        ASSERT_EQ(future.get()->resp_code, SnapshotRequest::Response::RESPONSE_OK);
    }

    static const SnapshotStatus* find(const LoggerStatus& status, const std::string& id) {
        for (const SnapshotStatus& snapshot : status.snapshots) {
            if (snapshot.id == id) {
                return &snapshot;
            }
        }
        return nullptr;
    }

    std::filesystem::path log_dir_;
    std::shared_ptr<basin::continuous_logger::LoggerNode> logger_;
    std::shared_ptr<rclcpp::Node> probe_;
    rclcpp::Subscription<LoggerStatus>::SharedPtr status_sub_;
    rclcpp::Subscription<FaultReport>::SharedPtr fault_sub_;
    rclcpp::Client<SnapshotRequest>::SharedPtr client_;
    std::unique_ptr<rclcpp::executors::SingleThreadedExecutor> executor_;
    std::optional<LoggerStatus> status_;
    std::vector<FaultReport> faults_;
};

/// A snapshot usage threshold every disk is at or above
class LoggerNodeDiskWarningTest : public LoggerNodeStatusTest {
   protected:
    double snapshotThresholdPercent() const override { return 0.0; }
};

/// A snapshot usage threshold no disk is at or above
class LoggerNodeDiskOkTest : public LoggerNodeStatusTest {
   protected:
    double snapshotThresholdPercent() const override { return 100.0; }
};

}  // namespace

TEST_F(LoggerNodeStatusTest, ReportsDiskAndConfiguredEvents) {
    spinUntilStatus([](const LoggerStatus&) { return true; });

    EXPECT_EQ(status_->log_path, log_dir_.string());
    EXPECT_EQ(status_->disk_rotation_threshold_percent, 91.0);
    EXPECT_EQ(status_->snapshot_usage_threshold_percent, 85.0);
    EXPECT_GE(status_->snapshot_usage_percent, 0.0);
    EXPECT_LT(status_->snapshot_usage_percent, 100.0);
    EXPECT_GT(status_->disk_total_bytes, 0u);
    EXPECT_LE(status_->disk_free_bytes, status_->disk_total_bytes);
    EXPECT_GT(status_->disk_usage_percent, 0.0);
    EXPECT_LT(status_->disk_usage_percent, 100.0);

    ASSERT_EQ(status_->events.size(), 1u);
    EXPECT_EQ(status_->events[0].event_name, "crash");
    EXPECT_EQ(status_->events[0].time_before, 4.0);
    EXPECT_EQ(status_->events[0].time_after, 1.5);
    EXPECT_EQ(status_->events[0].tags, (std::vector<std::string>{ "a", "b" }));
    EXPECT_EQ(status_->events[0].description, "a crash");
    EXPECT_EQ(status_->events[0].trigger_count, 0u);

    EXPECT_TRUE(status_->snapshots.empty());
    EXPECT_EQ(status_->total_snapshot_count, 0u);
}

TEST_F(LoggerNodeStatusTest, TracksAManualSnapshotFromStartToStop) {
    spinUntilStatus([](const LoggerStatus&) { return true; });

    callSnapshotService(SnapshotRequest::Request::REQUEST_START, "probe_snap");
    spinUntilStatus([](const LoggerStatus& status) { return find(status, "probe_snap") != nullptr; });
    const SnapshotStatus* running = find(*status_, "probe_snap");
    EXPECT_EQ(running->state, SnapshotStatus::STATE_RUNNING);
    EXPECT_EQ(running->end_time, -1);
    EXPECT_EQ(running->trigger, "manual");
    EXPECT_EQ(running->description, "made by the test");
    EXPECT_EQ(running->tags, (std::vector<std::string>{ "t" }));
    EXPECT_EQ(status_->total_snapshot_count, 1u);

    callSnapshotService(SnapshotRequest::Request::REQUEST_STOP, "probe_snap");
    spinUntilStatus([](const LoggerStatus& status) {
        const SnapshotStatus* snapshot = find(status, "probe_snap");
        return snapshot != nullptr && snapshot->state == SnapshotStatus::STATE_COMPLETE;
    });
    EXPECT_NE(find(*status_, "probe_snap")->end_time, -1);
    EXPECT_EQ(status_->total_snapshot_count, 1u);
}

TEST_F(LoggerNodeDiskWarningTest, RaisesAFaultWhileSnapshotUsageIsAtTheThreshold) {
    auto deadline = std::chrono::steady_clock::now() + TIMEOUT;
    while (findFault("disk_usage_high") == nullptr && std::chrono::steady_clock::now() < deadline) {
        executor_->spin_some(50ms);
    }
    const FaultReport* fault = findFault("disk_usage_high");
    ASSERT_NE(fault, nullptr) << "no disk_usage_high fault was reported";

    EXPECT_EQ(fault->key.fault_system, "continuous_logger");
    EXPECT_EQ(fault->source, "/logger");
    EXPECT_NE(fault->fault_desc.find("snapshot threshold"), std::string::npos) << fault->fault_desc;

    // It stays live for longer than one check interval, so a check that runs a little late doesn't let it lapse
    double live_for_s = (rclcpp::Time(fault->expiration_time) - rclcpp::Time(fault->raise_time)).seconds();
    EXPECT_GE(live_for_s, 2 * DISK_CHECK_INTERVAL_S - 1e-6);

    // It is re-reported by every check, not raised once: a fault lapses when its reports stop
    spinFor(700ms);
    size_t reports = 0;
    for (const FaultReport& report : faults_) {
        if (report.key.fault_name == "disk_usage_high") {
            reports++;
        }
    }
    EXPECT_GE(reports, 3u);
}

TEST_F(LoggerNodeDiskOkTest, RaisesNoFaultBelowTheSnapshotThreshold) {
    spinUntilStatus([](const LoggerStatus&) { return true; });
    // Several checks' worth
    spinFor(1500ms);

    EXPECT_EQ(findFault("disk_usage_high"), nullptr);
}
