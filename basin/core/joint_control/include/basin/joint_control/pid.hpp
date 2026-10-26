#pragma once

#include <limits>

namespace basin::joint_control {

/// @brief Simple PID controller for single-variable control loops.
/// @details Designed to be lightweight and reusable in ROS packages.
class PID {
   public:
    PID() = default;
    /// @brief Construct PID with gains and optional output limits
    /// @param kp Proportional gain
    /// @param ki Integral gain
    /// @param kd Derivative gain
    /// @param out_min Minimum output limit (clamped)
    /// @param out_max Maximum output limit (clamped)
    explicit PID(double kp, double ki, double kd, double out_min = -std::numeric_limits<double>::infinity(),
                 double out_max = std::numeric_limits<double>::infinity());

    /// @brief Set proportional, integral, and derivative gains
    /// @param kp Proportional gain
    /// @param ki Integral gain
    /// @param kd Derivative gain
    void setGains(double kp, double ki, double kd);

    /// @brief Set output limits (min <= max). Values outside limits will be clipped.
    /// @param min Minimum output
    /// @param max Maximum output
    void setOutputLimits(double min, double max);

    /// @brief Reset internal state (integrator and last error)
    void reset();

    /// @brief Compute control action from error and elapsed time dt (seconds)
    /// @param error Current error (setpoint - measurement)
    /// @param dt Elapsed time in seconds
    /// @return Control output (subject to configured limits)
    double update(double error, double dt);

    /// @brief Accessors for gains
    double kp() const { return kp_; }
    double ki() const { return ki_; }
    double kd() const { return kd_; }

    /// @brief Accessors for output limits
    double out_min() const { return out_min_; }
    double out_max() const { return out_max_; }

   private:
    double kp_{ 0.0 };
    double ki_{ 0.0 };
    double kd_{ 0.0 };

    double integrator_{ 0.0 };
    double last_error_{ 0.0 };
    bool first_update_{ true };

    double out_min_{ -std::numeric_limits<double>::infinity() };
    double out_max_{ std::numeric_limits<double>::infinity() };
};

}  // namespace basin::joint_control
