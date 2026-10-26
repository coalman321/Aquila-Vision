#include <gtest/gtest.h>

#include "basin/joint_control/pid.hpp"

using basin::joint_control::PID;

TEST(PIDTest, ProportionalOnly) {
    PID pid(2.0, 0.0, 0.0);
    double out = pid.update(1.0, 0.1);
    EXPECT_NEAR(out, 2.0, 1e-9);
}

TEST(PIDTest, IntegralAccumulation) {
    PID pid(0.0, 1.0, 0.0);
    pid.reset();
    double dt = 0.1;
    double sum = 0.0;
    for (int i = 0; i < 10; ++i) {
        double out = pid.update(1.0, dt);
        sum += dt;
        EXPECT_NEAR(out, sum, 1e-9);
    }
}

TEST(PIDTest, Derivative) {
    PID pid(0.0, 0.0, 1.0);
    pid.reset();
    // first update has no derivative
    double out0 = pid.update(0.0, 0.1);
    EXPECT_NEAR(out0, 0.0, 1e-9);
    double out1 = pid.update(2.0, 0.1);  // derivative = (2 - 0) / 0.1 = 20
    EXPECT_NEAR(out1, 20.0, 1e-9);
}

TEST(PIDTest, AntiWindupClamps) {
    PID pid(0.0, 10.0, 0.0, -1.0, 1.0);
    pid.reset();
    double dt = 0.1;
    for (int i = 0; i < 100; ++i) {
        double out = pid.update(1.0, dt);
        EXPECT_LE(out, 1.0 + 1e-9);
        EXPECT_GE(out, -1.0 - 1e-9);
    }
}
