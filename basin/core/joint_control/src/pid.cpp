#include "basin/joint_control/pid.hpp"

#include <algorithm>

namespace basin::joint_control {

PID::PID(double kp, double ki, double kd, double out_min, double out_max) :
    kp_(kp), ki_(ki), kd_(kd), out_min_(out_min), out_max_(out_max) {}

void PID::setGains(double kp, double ki, double kd) {
    kp_ = kp;
    ki_ = ki;
    kd_ = kd;
}

void PID::setOutputLimits(double min, double max) {
    out_min_ = min;
    out_max_ = max;
    if (out_max_ < out_min_) {
        std::swap(out_min_, out_max_);
    }
}

void PID::reset() {
    integrator_ = 0.0;
    last_error_ = 0.0;
    first_update_ = true;
}

double PID::update(double error, double dt) {
    if (dt <= 0.0) {
        return 0.0;
    }

    // Proportional term
    double p = kp_ * error;

    // Derivative term (band-limited derivative using error difference)
    double derivative = 0.0;
    if (first_update_) {
        // Nothing to differentiate on first call
        derivative = 0.0;
        first_update_ = false;
    } else {
        derivative = (error - last_error_) / dt;
    }
    double d = kd_ * derivative;

    last_error_ = error;

    // Calculate output without integral term to check for saturation
    double out_no_i = p + d;

    // Conditional integration anti-windup:
    // Only integrate if we're not saturated OR if integration would reduce saturation
    bool should_integrate = true;
    if (out_no_i > out_max_ && error > 0.0) {
        // Already saturated high and error would increase output further
        should_integrate = false;
    } else if (out_no_i < out_min_ && error < 0.0) {
        // Already saturated low and error would decrease output further
        should_integrate = false;
    }

    if (should_integrate) {
        integrator_ += error * dt;
    }

    double i = ki_ * integrator_;
    double out = p + i + d;

    // Final clamping to ensure output stays within limits
    out = std::clamp(out, out_min_, out_max_);

    return out;
}

}  // namespace basin::joint_control
