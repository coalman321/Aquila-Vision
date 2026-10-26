#pragma once

#include <chrono>
#include <process_msgs/msg/process_heartbeat.hpp>
#include <rclcpp/context.hpp>
#include <rclcpp/macros.hpp>
#include <rclcpp/node.hpp>
#include <rclcpp/publisher.hpp>
#include <rclcpp/time.hpp>
#include <rclcpp/timer.hpp>

namespace basin::monitor_core {

using namespace std::chrono_literals;

enum class ProcessState { STARTING = 0, RUNNING = 1, HALTING = 2, INVALID = 3 };

class HeartbeatSender {
   public:
    RCLCPP_SMART_PTR_DEFINITIONS_NOT_COPYABLE(HeartbeatSender)

    HeartbeatSender(rclcpp::Node* node);

    /// @brief Removes the pre-shutdown callback registered in the constructor. Without this,
    /// `rclcpp::shutdown()` invokes a callback capturing `this` for every `HeartbeatSender` ever
    /// constructed in the process — including ones already destroyed — which is a use-after-free.
    ~HeartbeatSender();

    /// @brief Initializes a timer to send running heartbeats for processes that dont have a main timer loop
    void startBeatTimer();

    /// @brief Sends a heartbeat message from this node corresponding to its current ProcessState
    /// @param state The current state of the process
    void sendBeat(ProcessState state);

   private:
    void sendShutdownBeat();

    ///< Heartbeat publisher used for sending beats
    rclcpp::Publisher<process_msgs::msg::ProcessHeartbeat>::SharedPtr beat_pub_;
    rclcpp::node_interfaces::NodeClockInterface::SharedPtr clock_ptr_;     ///< Clock used for stamping heartbeat times
    rclcpp::node_interfaces::NodeLoggingInterface::SharedPtr logger_ptr_;  ///< Logger used for logging messages
    std::string qualified_name_;  ///< Fully qualified name of the node used on startup

    rclcpp::TimerBase::SharedPtr beat_timer_;  ///< Optional timer used to tick on nodes that dont have a loop

    rclcpp::Context::SharedPtr context_;                 ///< Context the shutdown callback is registered on
    rclcpp::PreShutdownCallbackHandle shutdown_handle_;  ///< Handle used to unregister on destruction

    std::chrono::milliseconds pub_timeout_{
        500ms
    };  ///< final wait time for items to publish before completing shutdown
};

}  // namespace basin::monitor_core
