#pragma once

#include <basin/node_core/node_monitor.hpp>
#include <basin/node_core/rate_monitor.hpp>
#include <chrono>
#include <memory>
#include <rclcpp/node.hpp>
#include <rclcpp/subscription.hpp>
#include <rclcpp/timer.hpp>
#include <status_msgs/msg/fault_report.hpp>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "status_msgs/msg/fault_report.hpp"

namespace basin::node_core {

namespace detail {
/// @brief Hashes a FaultKey by its (fault_system, fault_name) fields, so it can key an
/// unordered_map -- the generated message type only provides operator==, not a std::hash.
struct FaultKeyHash {
    std::size_t operator()(const status_msgs::msg::FaultKey& key) const {
        return std::hash<std::string>()(key.fault_system) ^ (std::hash<std::string>()(key.fault_name) << 1);
    }
};
}  // namespace detail

class NodeCore : public rclcpp::Node {
   public:
    NodeCore(const std::string& node_name, const std::string& namespace_ = "",
             const rclcpp::NodeOptions& options = rclcpp::NodeOptions());

    /// @brief Called at a fixed interval defined by tick rate
    virtual void onTick() = 0;

    /// @brief Method called upon shutdown of the node
    virtual void shutdown() = 0;

    /// @brief Get the hostname of the machine
    std::string getHostname() const;

    /// @brief Send fault report
    /// @param fault_system The fault system identifier
    /// @param fault_name The fault name
    /// @param fault_tov The fault time of validity
    void sendFaultReport(const std::string& fault_system, const std::string& fault_name,
                         std::chrono::duration<double> fault_tov, const std::string & desc = "");

    /// @brief Registers `monitor` for automatic per-tick health checking.
    /// @param fault_system Fault system to raise under, passed to sendFaultReport() as-is.
    /// @param fault_name Fault name to raise while `monitor` is unhealthy.
    /// @param monitor Monitor to evaluate each tick; must outlive this NodeCore.
    /// @param fault_tov How long a raised fault stays valid before expiring.
    void registerRateMonitor(std::string fault_system, std::string fault_name, RateMonitor& monitor,
                             std::chrono::duration<double> fault_tov = std::chrono::duration<double>(2.0));

   protected:
    /// @brief Evaluates every registered RateMonitor and raises faults for unhealthy ones.
    /// Called automatically once per tick, before onTick() -- derived nodes shouldn't call this.
    /// @param now Time to evaluate the monitors at.
    void evaluateRateMonitors(RateMonitor::Clock::time_point now);

    /// @brief Publish seam `sendFaultReport` routes through, rather than publishing directly.
    /// Default is a plain publish; `basin::node_core::determinism::DeterministicNode` overrides
    /// this to additionally record publish provenance, since fault reports are node-logic
    /// output (unlike heartbeats, which stay out of the deterministic-replay surface entirely).
    virtual void publishFaultReport(const status_msgs::msg::FaultReport& report) { fault_report_pub_->publish(report); }

    std::unique_ptr<monitor_core::HeartbeatSender> heartbeat_;
    rclcpp::TimerBase::SharedPtr tick_timer_;
    rclcpp::Publisher<status_msgs::msg::FaultReport>::SharedPtr fault_report_pub_;

   private:
    struct RateMonitorRegistration {
        std::string fault_system;
        std::string fault_name;
        RateMonitor* monitor;
        std::chrono::duration<double> fault_tov;
        // Own throttle state (rather than RCLCPP_WARN_THROTTLE's, which throttles per call site,
        // not per fault) so every registration that's newly unhealthy in a cycle logs at least
        // once, and only that registration's own repeats are held off for the throttle period.
        RateMonitor::Clock::time_point last_logged_at{};
    };
    std::vector<RateMonitorRegistration> rate_monitors_;

    /// @brief Tracks the currently-active continuous period for a given fault key, so repeated
    /// sendFaultReport() calls for the same still-unexpired fault report the original raise time
    /// rather than resetting it on every call.
    struct ActiveFaultPeriod {
        rclcpp::Time first_raised;
        rclcpp::Time expiration;
    };
    std::unordered_map<status_msgs::msg::FaultKey, ActiveFaultPeriod, detail::FaultKeyHash> active_fault_periods_;
};

}  // namespace basin::node_core
