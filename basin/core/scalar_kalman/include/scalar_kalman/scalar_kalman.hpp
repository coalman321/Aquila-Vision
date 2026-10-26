#pragma once

namespace scalar_kalman {

/// Tunable parameters for the 1-D scalar Kalman filter.
struct Config {
    /// Per-measurement noise variance (m²). Lower = trust sensor more.
    double measurement_noise_variance{ 0.01 };
    /// Initial estimate covariance for an unseen cell (m²). Should be large relative to R.
    double initial_covariance{ 1.0 };
};

/// Filter state: current best estimate and its covariance.
struct State {
    double z_est{ 0.0 };
    double P{ 1.0 };
};

/// Initialize state from the first batch measurement for a new cell.
inline State init(double z_bar, const Config& cfg) { return { z_bar, cfg.initial_covariance }; }

/// Apply one Kalman update step.
/// z_bar: batch mean measurement; n: number of measurements in the batch.
/// The batch mean is treated as a single observation with noise R / n.
inline State update(State s, double z_bar, int n, const Config& cfg) {
    const double R_eff = cfg.measurement_noise_variance / static_cast<double>(n);
    const double K = s.P / (s.P + R_eff);
    return { s.z_est + K * (z_bar - s.z_est), (1.0 - K) * s.P };
}

}  // namespace scalar_kalman
