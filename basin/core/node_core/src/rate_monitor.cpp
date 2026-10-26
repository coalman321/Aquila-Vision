#include "basin/node_core/rate_monitor.hpp"

namespace basin::node_core {

std::string RateMonitor::Result::reason() const {
    switch (status) {
        case Status::NotReceived:
            return "no data received";
        case Status::Stalled:
            return "channel stalled";
        case Status::Erratic:
            return "off nominal rate";
        default:
            return "";
    }
}

std::string RateMonitor::Result::statusToken() const {
    switch (status) {
        case Status::NotReceived:
            return "not_received";
        case Status::Stalled:
            return "stalled";
        case Status::Erratic:
            return "erratic";
        default:
            return "";
    }
}

void RateMonitor::record(Clock::time_point stamp) {
    if (config_.expected_hz <= 0.0) {
        return;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!samples_.empty() && stamp < samples_.back()) {
        return;  // non-monotonic; ignore rather than corrupt the span estimate
    }
    samples_.push_back(stamp);
    received_any_ = true;

    // Bound memory even if evaluate() is called rarely: keep a little more than one window.
    const auto keepFrom = stamp - std::chrono::duration_cast<Clock::duration>(config_.window * 2);
    while (!samples_.empty() && samples_.front() < keepFrom) {
        samples_.pop_front();
    }
}

RateMonitor::Result RateMonitor::evaluate(Clock::time_point now) {
    Result result;

    if (config_.expected_hz <= 0.0) {
        result.status = Status::Disabled;
        return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto cutoff = now - std::chrono::duration_cast<Clock::duration>(config_.window);
    while (!samples_.empty() && samples_.front() < cutoff) {
        samples_.pop_front();
    }

    if (!received_any_) {
        result.status = Status::NotReceived;
        return result;
    }

    const double stallSec = config_.stall_periods / config_.expected_hz;

    if (samples_.empty()) {
        result.status = Status::Stalled;
        return result;
    }
    if (std::chrono::duration<double>(now - samples_.back()).count() > stallSec) {
        result.status = Status::Stalled;
        return result;
    }
    if (samples_.size() < config_.min_samples) {
        result.status = Status::Warmup;
        return result;
    }

    const double span = std::chrono::duration<double>(samples_.back() - samples_.front()).count();
    result.observed_hz = (span > 0.0) ? static_cast<double>(samples_.size() - 1) / span : 0.0;

    const double lo = config_.expected_hz * (1.0 - config_.tolerance);
    const double hi = config_.expected_hz * (1.0 + config_.tolerance);
    result.status = (result.observed_hz < lo || result.observed_hz > hi) ? Status::Erratic : Status::Ok;
    return result;
}

}  // namespace basin::node_core
