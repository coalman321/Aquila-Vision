#pragma once

#include <chrono>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>

namespace basin::node_core {

/// @brief Sliding-window output-rate monitor for a periodic signal (a sensor log channel, a
/// heartbeat, any source expected to arrive at roughly a fixed rate).
///
/// Records the arrival time of each sample and, on demand, classifies the source's recent
/// delivery rate against a configured nominal rate. Catches a source running persistently
/// off-rate (misconfigured, link saturated) and gaps/stalls (the source stopped emitting while
/// others keep flowing). Whole-device disconnection is a separate concern from this per-channel
/// check.
///
/// Internally synchronized: record() and evaluate() take a private mutex, so one thread may
/// record arrivals while another evaluates -- the common case of a receive thread feeding a
/// separate tick/health thread. The lock is only ever held for a short, bounded scan/prune, so
/// this comfortably covers sources up to several hundred Hz. Consequently the class is neither
/// copyable nor movable.
class RateMonitor {
   public:
    using Clock = std::chrono::steady_clock;

    struct Config {
        ///< Nominal output rate (Hz) this channel is configured to stream at. <= 0 disables the
        ///< monitor: evaluate() always returns Disabled.
        double expected_hz{ 0.0 };
        ///< Allowed fractional deviation of the observed rate from expected_hz before the
        ///< channel is reported Erratic (0.25 => accept 75%..125% of nominal).
        double tolerance{ 0.25 };
        ///< Width of the sliding window the observed rate is measured over. Should span at
        ///< least a handful of periods of the slowest monitored channel.
        std::chrono::duration<double> window{ std::chrono::seconds(5) };
        ///< A gap since the most recent sample longer than stall_periods nominal periods
        ///< (stall_periods / expected_hz seconds) is reported as Stalled.
        double stall_periods{ 5.0 };
        ///< Minimum in-window samples before a rate verdict is given; below this, Warmup.
        std::size_t min_samples{ 3 };
    };

    enum class Status {
        Disabled,     ///< expected_hz <= 0; channel not monitored.
        NotReceived,  ///< enabled, but no sample has been recorded yet since construction.
        Warmup,       ///< too few samples in the window yet to judge.
        Ok,           ///< observed rate within the tolerance band and no stall.
        Erratic,      ///< observed rate outside the tolerance band.
        Stalled,      ///< had samples before, but none within stall_periods nominal periods.
    };

    struct Result {
        Status status{ Status::Disabled };
        ///< Windowed rate estimate in Hz; 0.0 unless status is Ok or Erratic.
        double observed_hz{ 0.0 };

        /// @brief Whether this result represents a channel worth raising a fault over.
        bool unhealthy() const {
            return status == Status::Erratic || status == Status::Stalled || status == Status::NotReceived;
        }

        /// @brief Short human-readable reason for an unhealthy() result; empty otherwise.
        std::string reason() const;

        /// @brief Snake_case status token (e.g. "stalled"), suitable for composing into a fault
        /// name; empty when not unhealthy().
        std::string statusToken() const;
    };

    explicit RateMonitor(Config config) : config_(config) {}

    /// @brief Record the arrival of one sample. `stamp` must be monotonic across calls
    /// (steady_clock); out-of-order stamps are ignored.
    void record(Clock::time_point stamp);

    /// @brief Prune samples older than the window, then classify the channel. Non-const: prunes.
    Result evaluate(Clock::time_point now);

    const Config& config() const { return config_; }

   private:
    Config config_;
    std::mutex mutex_;
    std::deque<Clock::time_point> samples_;
    ///< Set on the first accepted record() call; distinguishes NotReceived (never any sample)
    ///< from Stalled (had samples, but the most recent one has aged out).
    bool received_any_{ false };
};

}  // namespace basin::node_core
