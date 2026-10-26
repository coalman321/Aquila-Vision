#pragma once

#include <filesystem>
#include <process_lib/system_monitor.hpp>
#include <process_msgs/msg/process_heartbeat.hpp>
#include <process_msgs/msg/process_list.hpp>
#include <process_msgs/msg/system_info.hpp>
#include <process_msgs/srv/remote_request.hpp>
#include <rclcpp/node.hpp>
#include <vector>

#include <basin/node_core/node_core.hpp>
#include "launch_core/controlled_process.hpp"
#include "launch_core/launch_interpreter.hpp"
#include "launch_core/launch_node_params.hpp"

namespace basin::launch_core {

class LaunchNode : public basin::node_core::NodeCore {
   public:
    LaunchNode(const rclcpp::NodeOptions& options);

    ~LaunchNode();

    bool loadLaunchFile(const std::string& launch_file, const std::string& launch_package, bool is_daemon = false);

    void startLaunch();

    void stopLaunch();

    bool isLaunchRunning();

    void onTick() override;

    void shutdown() override;

   private:
    void checkProcessesTimer();

    process_msgs::msg::ProcessInfo checkProcess(ControlledProcess& process, double watchdog_time,
                                                double cpu_total_delta);

    void heartbeatCallback(process_msgs::msg::ProcessHeartbeat::ConstSharedPtr msg);

    void processHeartbeat(std::vector<ControlledProcess>& processes, 
                          const process_msgs::msg::ProcessHeartbeat::ConstSharedPtr& msg);

    void requestCallback(process_msgs::srv::RemoteRequest::Request::SharedPtr req,
                         process_msgs::srv::RemoteRequest::Response::SharedPtr resp);

    ///< Params structure for this node
    std::unique_ptr<LaunchNodeParams> params_;

    ///< Name of this controller instance
    std::string controller_name_;

    ///< Vector of process controllers used to control the running children
    std::vector<ControlledProcess> process_controllers_;

    ///< Vector of process controllers for daemon processes
    std::vector<ControlledProcess> daemon_controllers_;

    ///< System resource monitor used to compute status percentages
    basin::process::SystemMonitor system_monitor_;

    ///< Currently active interpreter instance. nullptr if not active
    std::unique_ptr<LaunchInterpreter> interpreter_{ nullptr };

    ///< Publisher for the currently active process list
    rclcpp::Publisher<process_msgs::msg::ProcessList>::SharedPtr process_list_pub_;

    ///< Publisher for system resource info
    rclcpp::Publisher<process_msgs::msg::SystemInfo>::SharedPtr sys_info_pub_;

    ///< Subscriber for the heartbeat messages coming from the processes
    rclcpp::Subscription<process_msgs::msg::ProcessHeartbeat>::SharedPtr process_heartbeat_sub_;

    ///< Service server for handling requests
    rclcpp::Service<process_msgs::srv::RemoteRequest>::SharedPtr request_service_;
};
}  // namespace basin::launch_core
