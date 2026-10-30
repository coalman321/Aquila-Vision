#include <gtest/gtest.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>

#include "basin/log_util/log_manager.hpp"
#include "basin/log_util/log_writer.hpp"
#include "basin/log_util/snapshot_writer.hpp"

using basin::log_util::DiskCheckResult;
using basin::log_util::LogManager;
using basin::log_util::LogWriter;
using basin::log_util::SnapshotWriter;

namespace {

/// Runs a LogManager over a scratch log directory of real MCAP files. What the disk really uses is up to the
/// machine the test runs on, so tests pick thresholds no disk can be under (0) or over (100).
class LogManagerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("log_manager_test_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
        manager_.set_log_directory(dir_.string());
    }

    void TearDown() override { std::filesystem::remove_all(dir_); }

    /// Writes a log `<name>_000000.mcap` holding messages at `start_s` and `end_s` (epoch seconds), last written
    /// `age_s` seconds ago so tests control which log is oldest
    std::filesystem::path writeLog(const std::string& name, double start_s, double end_s, int age_s) {
        LogWriter writer;
        writer.open(basin::log_util::make_mcap_storage_options(dir_.string(), 3600, std::numeric_limits<long>::max()),
                    rosbag2_cpp::ConverterOptions(), name);
        logging_msgs::msg::LogMetadata metadata;
        metadata.key_name = "test";
        writer.write_metadata_event(metadata, rclcpp::Time(static_cast<int64_t>(start_s * 1e9)));
        writer.write_metadata_event(metadata, rclcpp::Time(static_cast<int64_t>(end_s * 1e9)));
        std::filesystem::path path = writer.get_current_file_path();
        writer.close();

        std::filesystem::last_write_time(path,
                                         std::filesystem::file_time_type::clock::now() - std::chrono::seconds(age_s));
        return path;
    }

    /// Records a snapshot over [start_s, end_s] in snapshots.yaml; an end of -1 leaves it running
    void addSnapshot(const std::string& id, int64_t start_s, int64_t end_s) {
        SnapshotWriter snapshots;
        snapshots.set_recording_directory(dir_.string());
        snapshots.load_from_file();
        snapshots.start_snapshot(id, "manual", "", {}, start_s);
        if (end_s != -1) {
            snapshots.stop_snapshot(id, end_s);
        }
    }

    std::filesystem::path dir_;
    LogManager manager_;
};

}  // namespace

TEST_F(LogManagerTest, RotatesEveryUnprotectedLogInOneCheck) {
    auto oldest = writeLog("log_0", 1000, 1010, 300);
    auto protected_log = writeLog("log_1", 2000, 2010, 200);
    auto newest = writeLog("log_2", 3000, 3010, 100);
    addSnapshot("snap", 2005, 2006);
    manager_.set_disk_limits(0.0, 100.0);

    DiskCheckResult result = manager_.check_disk("");

    EXPECT_EQ(result.rotated_files, 2u);
    EXPECT_FALSE(std::filesystem::exists(oldest));
    EXPECT_TRUE(std::filesystem::exists(protected_log));
    EXPECT_FALSE(std::filesystem::exists(newest));
    EXPECT_TRUE(result.rotation_blocked);
    EXPECT_EQ(result.protected_bytes, std::filesystem::file_size(protected_log));
}

TEST_F(LogManagerTest, RotatesNothingBelowTheThreshold) {
    auto log = writeLog("log_0", 1000, 1010, 100);
    manager_.set_disk_limits(100.0, 100.0);

    DiskCheckResult result = manager_.check_disk("");

    EXPECT_EQ(result.rotated_files, 0u);
    EXPECT_FALSE(result.rotation_blocked);
    EXPECT_TRUE(std::filesystem::exists(log));
}

TEST_F(LogManagerTest, RunningSnapshotProtectsEverythingFromItsStart) {
    auto before = writeLog("log_0", 1000, 1010, 300);
    auto during = writeLog("log_1", 2000, 2010, 200);
    auto after = writeLog("log_2", 3000, 3010, 100);
    addSnapshot("running", 2005, -1);
    manager_.set_disk_limits(0.0, 100.0);

    manager_.check_disk("");

    EXPECT_FALSE(std::filesystem::exists(before));
    EXPECT_TRUE(std::filesystem::exists(during));
    EXPECT_TRUE(std::filesystem::exists(after));
}

TEST_F(LogManagerTest, ProtectsALogEndingPartWayIntoASnapshotsFirstSecond) {
    // Spans 4.9 s from part-way through 2000 to part-way through 2005, so it overlaps a snapshot starting at 2005
    auto log = writeLog("log_0", 2000.6, 2005.5, 100);
    addSnapshot("snap", 2005, 2010);
    manager_.set_disk_limits(0.0, 100.0);

    manager_.check_disk("");

    EXPECT_TRUE(std::filesystem::exists(log));
}

TEST_F(LogManagerTest, NeverRotatesTheActiveLog) {
    auto oldest = writeLog("log_0", 1000, 1010, 200);
    auto active = writeLog("log_1", 2000, 2010, 100);
    manager_.set_disk_limits(0.0, 100.0);

    DiskCheckResult result = manager_.check_disk(active.string());

    EXPECT_EQ(result.rotated_files, 1u);
    EXPECT_FALSE(std::filesystem::exists(oldest));
    EXPECT_TRUE(std::filesystem::exists(active));
    EXPECT_TRUE(result.rotation_blocked);
}

TEST_F(LogManagerTest, ReportsSnapshotUsageAgainstItsThreshold) {
    auto log = writeLog("log_0", 2000, 2010, 100);
    addSnapshot("snap", 2000, 2010);

    manager_.set_disk_limits(100.0, 0.0);
    DiskCheckResult high = manager_.check_disk("");
    EXPECT_TRUE(high.snapshot_usage_high);
    EXPECT_EQ(high.protected_bytes, std::filesystem::file_size(log));
    EXPECT_GT(high.snapshot_usage_percent, 0.0);
    EXPECT_EQ(manager_.get_last_disk_check().protected_bytes, high.protected_bytes);

    manager_.set_disk_limits(100.0, 100.0);
    EXPECT_FALSE(manager_.check_disk("").snapshot_usage_high);
}

TEST_F(LogManagerTest, NextLogNameStartsAtZero) { EXPECT_EQ(manager_.next_log_name("log_"), "log_0"); }

TEST_F(LogManagerTest, NextLogNameFollowsTheHighestRunWhateverItsAge) {
    writeLog("log_3", 1000, 1001, 100);
    writeLog("log_7", 1000, 1001, 300);
    writeLog("other_9", 1000, 1001, 100);
    std::ofstream(dir_ / "log_12_000000.mcap").close();  // empty: left by a crash

    EXPECT_EQ(manager_.next_log_name("log_"), "log_8");
}

TEST_F(LogManagerTest, PrepareCreatesAMissingDirectory) {
    std::filesystem::remove_all(dir_);

    manager_.prepare_log_directory();

    EXPECT_TRUE(std::filesystem::is_directory(dir_));
}

TEST_F(LogManagerTest, PrepareRejectsAFile) {
    std::filesystem::remove_all(dir_);
    std::ofstream(dir_).close();

    EXPECT_THROW(manager_.prepare_log_directory(), std::runtime_error);
}
