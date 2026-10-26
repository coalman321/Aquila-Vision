
#include "basin/node_core/node_core.hpp"

#include <iomanip>
#include <sstream>
#include <unistd.h>

namespace basin::node_core {
NodeCore::NodeCore(const std::string& node_name, const std::string& namespace_, const rclcpp::NodeOptions& options) :
    rclcpp::Node(node_name, namespace_, options) {
    heartbeat_ = std::make_unique<monitor_core::HeartbeatSender>(this);

    // Set up tick timer
    double tick_period{ 0.1 };
    if (!has_parameter("core.tick_period")) {
        declare_parameter("core.tick_period", tick_period);
    }
    tick_period = get_parameter("core.tick_period").get_value<double>();
    tick_timer_ = create_timer(std::chrono::duration<double>(tick_period), [this]() {
        heartbeat_->sendBeat(basin::monitor_core::ProcessState::RUNNING);
        evaluateRateMonitors(std::chrono::steady_clock::now());
        onTick();
    });

    // Depth must comfortably exceed the number of distinct faults a single tick can raise
    // (evaluateRateMonitors() plus a derived node's own onTick()) -- a shallow KEEP_LAST history
    // lets a burst of same-tick publishes evict each other before they reach subscribers.
    fault_report_pub_ = create_publisher<status_msgs::msg::FaultReport>(
        "/state/fault_report", rclcpp::QoS(20).reliable().transient_local());

    // send the first heartbeat
    heartbeat_->sendBeat(basin::monitor_core::ProcessState::STARTING);
}

void NodeCore::sendFaultReport(const std::string& fault_system, const std::string& fault_name,
                               std::chrono::duration<double> fault_tov, const std::string & desc) {
    // get the current time
    rclcpp::Time now = get_clock()->now();

    // Look up whether this fault is already active (i.e. its last-reported expiration hasn't
    // passed yet). If so, this call is a continuation of that same period, so keep the original
    // raise time instead of resetting it to now; otherwise this is the start of a new period.
    status_msgs::msg::FaultKey key;
    key.fault_name = fault_name;
    key.fault_system = fault_system;

    rclcpp::Time raise_time = now;
    const auto existing = active_fault_periods_.find(key);
    if (existing != active_fault_periods_.end() && existing->second.expiration > now) {
        raise_time = existing->second.first_raised;
    }

    // build the report so that start time is the beginning of the current continuous period and
    // expiration is now + tov
    status_msgs::msg::FaultReport report;
    report.source = this->get_fully_qualified_name();
    report.key = key;
    report.raise_time = raise_time;
    report.expiration_time = now + rclcpp::Duration(fault_tov);
    report.fault_desc = desc;

    active_fault_periods_[key] = { raise_time, report.expiration_time };

    publishFaultReport(report);
}

void NodeCore::registerRateMonitor(std::string fault_system, std::string fault_name, RateMonitor& monitor,
                                   std::chrono::duration<double> fault_tov) {
    rate_monitors_.push_back({ std::move(fault_system), std::move(fault_name), &monitor, fault_tov });
}

void NodeCore::evaluateRateMonitors(RateMonitor::Clock::time_point now) {
    static constexpr auto kLogThrottlePeriod = std::chrono::seconds(5);

    for (auto& registration : rate_monitors_) {
        const auto result = registration.monitor->evaluate(now);
        if (!result.unhealthy()) {
            continue;
        }

        const std::string fault_name = registration.fault_name + "_" + result.statusToken();

        std::ostringstream desc_stream;
        desc_stream << result.reason() << " (observed " << std::fixed << std::setprecision(1) << result.observed_hz
                    << " Hz, expected " << registration.monitor->config().expected_hz << " Hz)";
        const std::string fault_desc = desc_stream.str();

        sendFaultReport(registration.fault_system, fault_name, registration.fault_tov, fault_desc);

        if (now - registration.last_logged_at >= kLogThrottlePeriod) {
            registration.last_logged_at = now;
            RCLCPP_WARN(get_logger(), "%s: %s", fault_name.c_str(), fault_desc.c_str());
        }
    }
}

std::string NodeCore::getHostname() const {
    char hostname[1024];
    gethostname(hostname, 1024);
    return std::string(hostname);
}
}  // namespace basin::node_core
