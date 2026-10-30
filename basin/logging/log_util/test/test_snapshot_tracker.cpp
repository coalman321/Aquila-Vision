#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "basin/log_util/snapshot_tracker.hpp"

using basin::log_util::SnapshotTracker;
using basin::log_util::SnapshotWriter;
using basin::log_util::WalState;

namespace {

rclcpp::Time at(double seconds) { return rclcpp::Time(static_cast<int64_t>(seconds * 1e9)); }

/// Runs SnapshotTrackers over a scratch log directory. A second tracker on the same directory stands in for the
/// next run after a crash.
class SnapshotTrackerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("snapshot_tracker_test_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
    }

    void TearDown() override { std::filesystem::remove_all(dir_); }

    std::unique_ptr<SnapshotTracker> makeTracker() {
        auto tracker = std::make_unique<SnapshotTracker>();
        tracker->set_recording_directory(dir_.string());
        return tracker;
    }

    WalState readWal() {
        SnapshotWriter writer;
        writer.set_recording_directory(dir_.string());
        WalState wal;
        writer.load_wal(wal);
        return wal;
    }

    std::filesystem::path dir_;
};

}  // namespace

TEST_F(SnapshotTrackerTest, WritesTheWalOnEveryTransition) {
    auto tracker = makeTracker();

    ASSERT_TRUE(tracker->start_manual("manual_snap", "by hand", { "t" }));
    ASSERT_EQ(readWal().open_snapshots.size(), 1u);
    EXPECT_EQ(readWal().open_snapshots[0].id, "manual_snap");

    std::string event_id = tracker->start_event("crash", at(1000), 5.0, 2.5, "a crash", {});
    EXPECT_EQ(event_id, "event_crash_0");
    ASSERT_EQ(readWal().pending_event_snapshots.size(), 1u);
    EXPECT_EQ(readWal().pending_event_snapshots[0].finalize_time_sec, 1002);
    EXPECT_EQ(readWal().pending_event_snapshots[0].finalize_time_nsec, 500000000);
    EXPECT_EQ(tracker->get_snapshot(event_id).start_time, 995);

    ASSERT_TRUE(tracker->stop_manual("manual_snap"));
    EXPECT_TRUE(readWal().open_snapshots.empty());
    EXPECT_EQ(readWal().pending_event_snapshots.size(), 1u);
}

TEST_F(SnapshotTrackerTest, FinalizesEventsOnceTheirWindowEnds) {
    auto tracker = makeTracker();
    std::string id = tracker->start_event("crash", at(1000), 5.0, 2.0, "", {});

    EXPECT_TRUE(tracker->finalize_due(at(1001)).empty());
    EXPECT_EQ(tracker->pending_event_ids().count(id), 1u);

    EXPECT_EQ(tracker->finalize_due(at(1002)), std::vector<std::string>{ id });
    EXPECT_TRUE(tracker->pending_event_ids().empty());
    EXPECT_EQ(tracker->get_snapshot(id).end_time, 1002);
    EXPECT_TRUE(readWal().pending_event_snapshots.empty());
}

TEST_F(SnapshotTrackerTest, RecoversWhatACrashedRunLeftOpen) {
    {
        auto crashed = makeTracker();
        crashed->start_manual("manual_snap", "", {});
        crashed->start_event("overdue", at(1000), 1.0, 1.0, "", {});
        crashed->start_event("later", at(1000), 1.0, 100.0, "", {});
        // Destroyed without stopping anything, as a crash would leave it
    }

    auto tracker = makeTracker();
    std::vector<std::string> finalized = tracker->recover(at(1050));

    EXPECT_EQ(finalized, (std::vector<std::string>{ "manual_snap", "event_overdue_0" }));
    EXPECT_EQ(tracker->get_snapshot("manual_snap").end_time, 1050);
    EXPECT_EQ(tracker->get_snapshot("event_overdue_0").end_time, 1001);

    // The event still inside its window is re-armed and finalized when it ends
    EXPECT_EQ(tracker->pending_event_ids(), (std::set<std::string>{ "event_later_0" }));
    EXPECT_TRUE(readWal().open_snapshots.empty());
    EXPECT_EQ(readWal().pending_event_snapshots.size(), 1u);
    EXPECT_EQ(tracker->finalize_due(at(1100)), std::vector<std::string>{ "event_later_0" });
}

TEST_F(SnapshotTrackerTest, CountersContinueFromExistingSnapshots) {
    {
        auto earlier = makeTracker();
        earlier->start_event("with_underscore", at(1000), 1.0, 1.0, "", {});
        earlier->start_event("with_underscore", at(1010), 1.0, 1.0, "", {});
        earlier->finalize_due(at(2000));
    }

    auto tracker = makeTracker();
    tracker->recover(at(3000));

    EXPECT_EQ(tracker->event_counters().at("with_underscore"), 2u);
    EXPECT_EQ(tracker->start_event("with_underscore", at(3000), 1.0, 1.0, "", {}), "event_with_underscore_2");
}

TEST_F(SnapshotTrackerTest, FinalizePendingLeavesOnlyManualSnapshotsInTheWal) {
    auto tracker = makeTracker();
    tracker->start_manual("manual_snap", "", {});
    std::string id = tracker->start_event("crash", at(1000), 1.0, 100.0, "", {});

    EXPECT_EQ(tracker->finalize_pending(at(1010)), std::vector<std::string>{ id });

    EXPECT_EQ(tracker->get_snapshot(id).end_time, 1010);
    EXPECT_TRUE(readWal().pending_event_snapshots.empty());
    ASSERT_EQ(readWal().open_snapshots.size(), 1u);
    EXPECT_EQ(readWal().open_snapshots[0].id, "manual_snap");
}

TEST_F(SnapshotTrackerTest, RejectsDuplicateAndUnknownManualSnapshots) {
    auto tracker = makeTracker();
    ASSERT_TRUE(tracker->start_manual("snap", "", {}));

    EXPECT_FALSE(tracker->start_manual("snap", "", {}));
    EXPECT_FALSE(tracker->stop_manual("missing"));
    ASSERT_TRUE(tracker->stop_manual("snap"));
    EXPECT_FALSE(tracker->stop_manual("snap"));
}
