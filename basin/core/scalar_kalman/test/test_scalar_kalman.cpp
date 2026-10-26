#include <gtest/gtest.h>

#include <cmath>
#include <random>

#include "scalar_kalman/scalar_kalman.hpp"

namespace {

const scalar_kalman::Config kDefaultCfg{ 0.01, 1.0 };

TEST(ScalarKalmanTest, InitSetsMeasurementAsEstimate) {
    auto s = scalar_kalman::init(3.5, kDefaultCfg);
    EXPECT_DOUBLE_EQ(s.z_est, 3.5);
    EXPECT_DOUBLE_EQ(s.P, kDefaultCfg.initial_covariance);
}

TEST(ScalarKalmanTest, SingleUpdateMovesEstimateTowardMeasurement) {
    auto s = scalar_kalman::init(0.0, kDefaultCfg);
    auto s2 = scalar_kalman::update(s, 1.0, 1, kDefaultCfg);
    EXPECT_GT(s2.z_est, 0.0);
    EXPECT_LT(s2.z_est, 1.0);
    EXPECT_LT(s2.P, s.P);
}

TEST(ScalarKalmanTest, CovarianceMonotonicallyDecreases) {
    auto s = scalar_kalman::init(0.0, kDefaultCfg);
    for (int i = 0; i < 20; ++i) {
        auto s_next = scalar_kalman::update(s, 0.0, 1, kDefaultCfg);
        EXPECT_LT(s_next.P, s.P);
        s = s_next;
    }
}

TEST(ScalarKalmanTest, ConvergesToTrueHeightUnderNoise) {
    const double true_z = 2.5;
    std::mt19937 rng(42);
    std::normal_distribution<double> noise(true_z, 0.1);

    auto s = scalar_kalman::init(noise(rng), kDefaultCfg);
    for (int i = 1; i < 50; ++i) {
        s = scalar_kalman::update(s, noise(rng), 1, kDefaultCfg);
    }
    EXPECT_NEAR(s.z_est, true_z, 0.1);
    EXPECT_LT(s.P, 0.01);
}

TEST(ScalarKalmanTest, BatchOfNReducesNoiseByN) {
    // A batch mean of n measurements should drive covariance down faster than n single updates.
    const double z = 1.0;
    auto s_single = scalar_kalman::init(0.0, kDefaultCfg);
    auto s_batch = scalar_kalman::init(0.0, kDefaultCfg);

    for (int i = 0; i < 10; ++i) {
        s_single = scalar_kalman::update(s_single, z, 1, kDefaultCfg);
    }
    s_batch = scalar_kalman::update(s_batch, z, 10, kDefaultCfg);

    EXPECT_NEAR(s_single.P, s_batch.P, 1e-9);
    EXPECT_NEAR(s_single.z_est, s_batch.z_est, 1e-9);
}

}  // namespace
