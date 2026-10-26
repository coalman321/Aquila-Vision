#pragma once

#include <functional>
#include <optional>
#include <rclcpp/duration.hpp>
#include <rclcpp/time.hpp>
#include <utility>

namespace basin::node_core {

/// @brief Time-stable debouncer for a discrete signal (bool, enum, small int, ...).
///
/// A new sample value only becomes the committed output once it has been observed
/// continuously for at least the settle window — any sample that disagrees with the
/// pending candidate before then restarts the window. Samples matching the current
/// committed value clear any pending candidate immediately (a glitch that returns to
/// the steady state costs nothing).
///
/// The settle window can differ per edge. Construct with a single duration for a
/// symmetric window, or with a `(rising, falling)` pair to require different dwell
/// times for a rising edge (candidate is truthy — activation) versus a falling edge
/// (candidate is falsy — deactivation). This is the common need for limit switches
/// and enable flags, where you want to trust engagement quickly but release slowly
/// (or vice versa). For full control, pass a selector that returns the window for a
/// given candidate value.
///
/// Time is supplied by the caller rather than read from a clock, so the class carries
/// no `rclcpp::Node`/clock dependency and stays usable under deterministic replay and
/// in unit tests. Pass timestamps from a single monotonic source; a backwards jump is
/// treated as a fresh observation of the candidate (the window restarts from `now`).
///
/// @tparam T Signal type. Must be equality-comparable and copyable. The `(rising,
///           falling)` constructor and `setSettleDuration` overload additionally
///           require `T` to be contextually convertible to `bool`.
template <typename T = bool>
class Debouncer {
   public:
    /// @brief Returns the settle window a given candidate value must dwell for before
    /// it is committed.
    using SettleSelector = std::function<rclcpp::Duration(const T& candidate)>;

    /// @param initial Value the debouncer reports until a different value settles.
    /// @param settle Minimum duration a new value must persist before it is committed.
    Debouncer(T initial, rclcpp::Duration settle) : Debouncer(std::move(initial), symmetricSelector(settle)) {}

    /// @param initial Value the debouncer reports until a different value settles.
    /// @param rising_settle Dwell required for a rising edge (candidate converts to `true`).
    /// @param falling_settle Dwell required for a falling edge (candidate converts to `false`).
    Debouncer(T initial, rclcpp::Duration rising_settle, rclcpp::Duration falling_settle) :
        Debouncer(std::move(initial), edgeSelector(rising_settle, falling_settle)) {}

    /// @param initial Value the debouncer reports until a different value settles.
    /// @param settle_for Returns the dwell required before a given candidate is committed.
    Debouncer(T initial, SettleSelector settle_for) :
        value_(std::move(initial)), candidate_(value_), settle_for_(std::move(settle_for)) {}

    /// @brief Feed one raw sample observed at `now`.
    /// @return true if the committed value changed as a result of this call.
    bool update(const T& sample, const rclcpp::Time& now) {
        if (sample == value_) {
            // Back to (or still at) the steady state — drop any pending change.
            candidate_since_.reset();
            candidate_ = value_;
            return false;
        }

        if (!candidate_since_ || sample != candidate_ || now < *candidate_since_) {
            // First disagreeing sample, a different candidate than last time, or a
            // backwards time jump — (re)start the settle window from here.
            candidate_ = sample;
            candidate_since_ = now;
            return false;
        }

        if (now - *candidate_since_ >= settle_for_(candidate_)) {
            value_ = candidate_;
            candidate_since_.reset();
            return true;
        }

        return false;
    }

    /// @brief The current committed (debounced) value.
    const T& value() const { return value_; }

    /// @brief Whether a different value is currently waiting out the settle window.
    bool pending() const { return candidate_since_.has_value(); }

    /// @brief The candidate value waiting to be committed, if any.
    std::optional<T> pendingValue() const { return candidate_since_ ? std::optional<T>(candidate_) : std::nullopt; }

    /// @brief The settle window that would apply to `candidate`.
    rclcpp::Duration settleDurationFor(const T& candidate) const { return settle_for_(candidate); }

    /// @brief Switch to a symmetric settle window. Applies to the in-flight candidate too.
    void setSettleDuration(rclcpp::Duration settle) { settle_for_ = symmetricSelector(settle); }

    /// @brief Switch to per-edge settle windows. Applies to the in-flight candidate too.
    void setSettleDuration(rclcpp::Duration rising_settle, rclcpp::Duration falling_settle) {
        settle_for_ = edgeSelector(rising_settle, falling_settle);
    }

    /// @brief Switch to a custom settle selector. Applies to the in-flight candidate too.
    void setSettleSelector(SettleSelector settle_for) { settle_for_ = std::move(settle_for); }

    /// @brief Force the committed value to `value`, discarding any pending candidate.
    void reset(T value) {
        value_ = std::move(value);
        candidate_ = value_;
        candidate_since_.reset();
    }

   private:
    static SettleSelector symmetricSelector(rclcpp::Duration settle) {
        return [settle](const T&) { return settle; };
    }

    static SettleSelector edgeSelector(rclcpp::Duration rising_settle, rclcpp::Duration falling_settle) {
        return [rising_settle, falling_settle](const T& candidate) {
            return static_cast<bool>(candidate) ? rising_settle : falling_settle;
        };
    }

    T value_;                                      ///< Committed output.
    T candidate_;                                  ///< Value currently being timed out.
    SettleSelector settle_for_;                    ///< Maps a candidate to its required dwell.
    std::optional<rclcpp::Time> candidate_since_;  ///< When the candidate was first seen.
};

}  // namespace basin::node_core
