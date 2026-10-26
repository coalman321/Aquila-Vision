#include <gtest/gtest.h>

#include <basin/node_core/debouncer.hpp>
#include <cstdlib>
#include <optional>

using basin::node_core::Debouncer;

namespace {

rclcpp::Time t(int64_t ms) { return rclcpp::Time(ms * 1000 * 1000, RCL_ROS_TIME); }
const rclcpp::Duration kSettle = rclcpp::Duration::from_seconds(0.1);  // 100 ms

}  // namespace

TEST(Debouncer, HoldsInitialUntilChangeSettles) {
    Debouncer<bool> d(false, kSettle);

    EXPECT_FALSE(d.update(true, t(0)));  // candidate starts
    EXPECT_FALSE(d.value());
    EXPECT_TRUE(d.pending());
    EXPECT_FALSE(d.update(true, t(50)));  // still inside window
    EXPECT_FALSE(d.value());

    EXPECT_TRUE(d.update(true, t(100)));  // window elapsed -> commit
    EXPECT_TRUE(d.value());
    EXPECT_FALSE(d.pending());
}

TEST(Debouncer, GlitchBackToSteadyStateResetsWindow) {
    Debouncer<bool> d(false, kSettle);

    EXPECT_FALSE(d.update(true, t(0)));
    EXPECT_FALSE(d.update(false, t(50)));  // glitch clears the pending change
    EXPECT_FALSE(d.pending());

    EXPECT_FALSE(d.update(true, t(60)));   // window restarts at 60
    EXPECT_FALSE(d.update(true, t(150)));  // only 90 ms elapsed
    EXPECT_TRUE(d.update(true, t(160)));   // 100 ms elapsed -> commit
    EXPECT_TRUE(d.value());
}

TEST(Debouncer, ChangingCandidateRestartsWindow) {
    Debouncer<int> d(0, kSettle);

    EXPECT_FALSE(d.update(1, t(0)));
    EXPECT_FALSE(d.update(2, t(80)));  // new candidate, window restarts
    EXPECT_EQ(d.pendingValue(), std::optional<int>(2));
    EXPECT_FALSE(d.update(2, t(150)));  // 70 ms since candidate 2
    EXPECT_TRUE(d.update(2, t(180)));   // 100 ms -> commit 2
    EXPECT_EQ(d.value(), 2);
}

TEST(Debouncer, BackwardsTimeJumpRestartsWindow) {
    Debouncer<bool> d(false, kSettle);

    EXPECT_FALSE(d.update(true, t(1000)));
    EXPECT_FALSE(d.update(true, t(500)));  // clock went backwards -> restart
    EXPECT_FALSE(d.update(true, t(550)));
    EXPECT_TRUE(d.update(true, t(600)));  // 100 ms after the restart
    EXPECT_TRUE(d.value());
}

TEST(Debouncer, ResetForcesValueAndClearsPending) {
    Debouncer<bool> d(false, kSettle);

    EXPECT_FALSE(d.update(true, t(0)));
    EXPECT_TRUE(d.pending());

    d.reset(true);
    EXPECT_TRUE(d.value());
    EXPECT_FALSE(d.pending());
    EXPECT_FALSE(d.update(true, t(1000)));  // already at this value, nothing to do
}

TEST(Debouncer, SettleDurationIsConfigurable) {
    Debouncer<bool> d(false, kSettle);
    d.setSettleDuration(rclcpp::Duration::from_seconds(0.2));
    EXPECT_EQ(d.settleDurationFor(true), rclcpp::Duration::from_seconds(0.2));

    EXPECT_FALSE(d.update(true, t(0)));
    EXPECT_FALSE(d.update(true, t(150)));
    EXPECT_TRUE(d.update(true, t(200)));
}

TEST(Debouncer, AsymmetricRisingAndFallingWindows) {
    const auto rising = rclcpp::Duration::from_seconds(0.03);   // 30 ms to trust engagement
    const auto falling = rclcpp::Duration::from_seconds(0.15);  // 150 ms to trust release
    Debouncer<bool> d(false, rising, falling);

    EXPECT_EQ(d.settleDurationFor(true), rising);
    EXPECT_EQ(d.settleDurationFor(false), falling);

    // Rising edge settles fast.
    EXPECT_FALSE(d.update(true, t(0)));
    EXPECT_FALSE(d.update(true, t(20)));
    EXPECT_TRUE(d.update(true, t(30)));
    EXPECT_TRUE(d.value());

    // Falling edge from the same signal needs the longer window.
    EXPECT_FALSE(d.update(false, t(100)));
    EXPECT_FALSE(d.update(false, t(200)));  // only 100 ms -> still true
    EXPECT_TRUE(d.value());
    EXPECT_TRUE(d.update(false, t(250)));  // 150 ms -> commit false
    EXPECT_FALSE(d.value());
}

TEST(Debouncer, EdgeWindowSwitchAppliesToInFlightCandidate) {
    Debouncer<bool> d(false, kSettle);

    EXPECT_FALSE(d.update(true, t(0)));
    d.setSettleDuration(rclcpp::Duration::from_seconds(0.03),   // rising
                        rclcpp::Duration::from_seconds(0.30));  // falling
    EXPECT_TRUE(d.update(true, t(40)));                         // now judged against the 30 ms rising window
    EXPECT_TRUE(d.value());
}

TEST(Debouncer, CustomSelectorPerCandidateValue) {
    // Longer dwell the further the candidate is from zero.
    Debouncer<int> d(0, [](int candidate) { return rclcpp::Duration::from_seconds(0.05 * std::abs(candidate)); });

    EXPECT_FALSE(d.update(1, t(0)));
    EXPECT_TRUE(d.update(1, t(50)));  // |1| * 50 ms
    EXPECT_EQ(d.value(), 1);

    EXPECT_FALSE(d.update(3, t(100)));
    EXPECT_FALSE(d.update(3, t(200)));  // needs 150 ms
    EXPECT_TRUE(d.update(3, t(260)));
    EXPECT_EQ(d.value(), 3);
}
