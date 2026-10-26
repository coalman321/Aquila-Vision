#include <gtest/gtest.h>

#include "continous_logger/logger_status.hpp"

using basin::continuous_logger::EventSnapshotConfig;
using basin::continuous_logger::makeEventStatuses;
using basin::continuous_logger::makeSnapshotStatuses;
using basin::log_util::SnapshotInfo;
using logging_msgs::msg::SnapshotStatus;

namespace {

SnapshotInfo snapshot(const std::string& id, int64_t start_time, int64_t end_time) {
    return SnapshotInfo{ id, start_time, end_time, "manual", "description of " + id, { "tag" } };
}

std::vector<std::string> ids(const std::vector<SnapshotStatus>& statuses) {
    std::vector<std::string> result;
    for (const SnapshotStatus& status : statuses) {
        result.push_back(status.id);
    }
    return result;
}

}  // namespace

TEST(EventStatuses, CarriesConfigAndTriggerCount) {
    EventSnapshotConfig crash;
    crash.event_name = "crash";
    crash.time_before = 10.0;
    crash.time_after = 2.5;
    crash.tags = { "a", "b" };
    crash.description = "a crash";
    EventSnapshotConfig quiet;
    quiet.event_name = "quiet";

    auto events = makeEventStatuses({ crash, quiet }, { { "crash", 3 }, { "unrelated", 9 } });

    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].event_name, "crash");
    EXPECT_EQ(events[0].time_before, 10.0);
    EXPECT_EQ(events[0].time_after, 2.5);
    EXPECT_EQ(events[0].tags, (std::vector<std::string>{ "a", "b" }));
    EXPECT_EQ(events[0].description, "a crash");
    EXPECT_EQ(events[0].trigger_count, 3u);
    // An event that has not triggered has no counter yet
    EXPECT_EQ(events[1].event_name, "quiet");
    EXPECT_EQ(events[1].trigger_count, 0u);
}

TEST(EventStatuses, EmptyWithoutConfigs) { EXPECT_TRUE(makeEventStatuses({}, { { "crash", 3 } }).empty()); }

TEST(SnapshotStatuses, StateComesFromEndTimeAndPendingSet) {
    auto statuses = makeSnapshotStatuses(
        { snapshot("done", 10, 20), snapshot("manual_open", 30, -1), snapshot("event_x_0", 40, -1) }, { "event_x_0" });

    ASSERT_EQ(statuses.size(), 3u);
    EXPECT_EQ(statuses[0].id, "event_x_0");
    EXPECT_EQ(statuses[0].state, SnapshotStatus::STATE_PENDING);
    EXPECT_EQ(statuses[0].end_time, -1);
    EXPECT_EQ(statuses[1].id, "manual_open");
    EXPECT_EQ(statuses[1].state, SnapshotStatus::STATE_RUNNING);
    EXPECT_EQ(statuses[2].id, "done");
    EXPECT_EQ(statuses[2].state, SnapshotStatus::STATE_COMPLETE);
    EXPECT_EQ(statuses[2].start_time, 10);
    EXPECT_EQ(statuses[2].end_time, 20);
    EXPECT_EQ(statuses[2].trigger, "manual");
    EXPECT_EQ(statuses[2].description, "description of done");
    EXPECT_EQ(statuses[2].tags, (std::vector<std::string>{ "tag" }));
}

TEST(SnapshotStatuses, ActiveFirstThenCompletedNewestFirst) {
    auto statuses = makeSnapshotStatuses({ snapshot("old", 1, 2), snapshot("open_old", 5, -1), snapshot("new", 100, 110),
                                           snapshot("mid", 50, 60), snapshot("open_new", 90, -1) },
                                         {});

    EXPECT_EQ(ids(statuses), (std::vector<std::string>{ "open_new", "open_old", "new", "mid", "old" }));
}

TEST(SnapshotStatuses, CapsCompletedButNeverActive) {
    std::vector<SnapshotInfo> snapshots;
    for (int i = 0; i < 10; i++) {
        snapshots.push_back(snapshot("done_" + std::to_string(i), 100 + i, 200 + i));
    }
    // Older than every completed one, and still listed
    snapshots.push_back(snapshot("open", 1, -1));

    auto statuses = makeSnapshotStatuses(snapshots, {}, 3);

    EXPECT_EQ(ids(statuses), (std::vector<std::string>{ "open", "done_9", "done_8", "done_7" }));
}

TEST(SnapshotStatuses, EmptyWithoutSnapshots) { EXPECT_TRUE(makeSnapshotStatuses({}, {}).empty()); }
