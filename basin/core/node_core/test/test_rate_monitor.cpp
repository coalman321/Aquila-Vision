#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "basin/node_core/rate_monitor.hpp"

using basin::node_core::RateMonitor;
using Clock = RateMonitor::Clock;
using Status = RateMonitor::Status;

namespace {

RateMonitor::Config config(double expectedHz) {
    RateMonitor::Config c;
    c.expected_hz = expectedHz;
    c.tolerance = 0.25;
    c.window = std::chrono::duration<double>(5.0);
    c.stall_periods = 5.0;
    c.min_samples = 3;
    return c;
}

// Feed `count` samples spaced `periodMs` apart starting at `start`; returns the time of the
// last sample.
Clock::time_point feed(RateMonitor& mon, Clock::time_point start, int count, int periodMs) {
    Clock::time_point t = start;
    for (int i = 0; i < count; ++i) {
        t = start + std::chrono::milliseconds(static_cast<int64_t>(i) * periodMs);
        mon.record(t);
    }
    return t;
}

}  // namespace

TEST(RateMonitor, DisabledWhenExpectedRateNonPositive) {
    RateMonitor mon(config(0.0));
    const Clock::time_point base{};
    feed(mon, base, 100, 10);
    EXPECT_EQ(mon.evaluate(base + std::chrono::seconds(1)).status, Status::Disabled);
}

TEST(RateMonitor, WarmupBeforeEnoughSamples) {
    RateMonitor mon(config(10.0));
    const Clock::time_point base{};
    mon.record(base);
    mon.record(base + std::chrono::milliseconds(100));
    EXPECT_EQ(mon.evaluate(base + std::chrono::milliseconds(150)).status, Status::Warmup);
}

TEST(RateMonitor, OkWhenRateMatchesExpected) {
    RateMonitor mon(config(10.0));
    const Clock::time_point base{};
    const auto last = feed(mon, base, 40, 100);  // 10 Hz
    const auto res = mon.evaluate(last + std::chrono::milliseconds(50));
    EXPECT_EQ(res.status, Status::Ok);
    EXPECT_NEAR(res.observed_hz, 10.0, 0.5);
}

TEST(RateMonitor, ErraticWhenRunningSlow) {
    RateMonitor mon(config(10.0));
    const Clock::time_point base{};
    const auto last = feed(mon, base, 25, 143);  // ~7 Hz, below 7.5 Hz lower bound
    const auto res = mon.evaluate(last + std::chrono::milliseconds(50));
    EXPECT_EQ(res.status, Status::Erratic);
    EXPECT_LT(res.observed_hz, 7.5);
}

TEST(RateMonitor, ErraticWhenRunningFast) {
    RateMonitor mon(config(10.0));
    const Clock::time_point base{};
    const auto last = feed(mon, base, 80, 50);  // 20 Hz
    EXPECT_EQ(mon.evaluate(last + std::chrono::milliseconds(20)).status, Status::Erratic);
}

TEST(RateMonitor, StalledWhenLastSampleTooOld) {
    RateMonitor mon(config(10.0));
    const Clock::time_point base{};
    const auto last = feed(mon, base, 40, 100);
    // stall threshold = 5 periods = 0.5 s; evaluate 1 s after the last sample.
    EXPECT_EQ(mon.evaluate(last + std::chrono::seconds(1)).status, Status::Stalled);
}

TEST(RateMonitor, NotReceivedWhenNeverAnySamples) {
    RateMonitor mon(config(10.0));
    const Clock::time_point base{};
    EXPECT_EQ(mon.evaluate(base + std::chrono::seconds(30)).status, Status::NotReceived);
}

TEST(RateMonitor, StalledRatherThanNotReceivedAfterPriorSamplesAgeOut) {
    RateMonitor mon(config(10.0));
    const Clock::time_point base{};
    const auto last = feed(mon, base, 40, 100);
    // Well past both the stall threshold and the window -- samples_ is empty again, but this
    // channel has received data before, so it should read as Stalled, not NotReceived.
    EXPECT_EQ(mon.evaluate(last + std::chrono::seconds(30)).status, Status::Stalled);
}

TEST(RateMonitor, RecoversToOkAfterAStall) {
    RateMonitor mon(config(10.0));
    const Clock::time_point base{};
    feed(mon, base, 40, 100);
    ASSERT_EQ(mon.evaluate(base + std::chrono::seconds(10)).status, Status::Stalled);

    const auto resumeStart = base + std::chrono::seconds(11);
    const auto last = feed(mon, resumeStart, 40, 100);
    EXPECT_EQ(mon.evaluate(last + std::chrono::milliseconds(50)).status, Status::Ok);
}

TEST(RateMonitorResult, UnhealthyAndReasonMatchStatus) {
    RateMonitor::Result result;

    result.status = Status::Disabled;
    EXPECT_FALSE(result.unhealthy());

    result.status = Status::NotReceived;
    EXPECT_TRUE(result.unhealthy());
    EXPECT_EQ(result.reason(), "no data received");
    EXPECT_EQ(result.statusToken(), "not_received");

    result.status = Status::Warmup;
    EXPECT_FALSE(result.unhealthy());

    result.status = Status::Ok;
    EXPECT_FALSE(result.unhealthy());

    result.status = Status::Erratic;
    EXPECT_TRUE(result.unhealthy());
    EXPECT_EQ(result.reason(), "off nominal rate");
    EXPECT_EQ(result.statusToken(), "erratic");

    result.status = Status::Stalled;
    EXPECT_TRUE(result.unhealthy());
    EXPECT_EQ(result.reason(), "channel stalled");
    EXPECT_EQ(result.statusToken(), "stalled");
}

// Runs record() from one thread and evaluate() from another concurrently, the pattern
// RateMonitor's internal locking exists for (e.g. a receive thread vs. a tick thread). Not a
// correctness proof of the locking, but under tsan/asan this reliably catches a regression to
// unsynchronized access.
TEST(RateMonitor, ConcurrentRecordAndEvaluateDoNotRace) {
    RateMonitor mon(config(500.0));
    std::atomic<bool> stop{ false };

    std::thread recorder([&] {
        while (!stop.load(std::memory_order_relaxed)) {
            mon.record(Clock::now());
        }
    });

    for (int i = 0; i < 2000; ++i) {
        (void)mon.evaluate(Clock::now());
    }

    stop.store(true, std::memory_order_relaxed);
    recorder.join();
}
