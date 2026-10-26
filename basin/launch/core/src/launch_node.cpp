#include "launch_core/launch_node.hpp"

#include <ament_index_cpp/get_package_prefix.hpp>
#include <basin/node_core/node_core.hpp>
#include <chrono>
#include <process_lib/process_controller.hpp>
#include <rclcpp/duration.hpp>
#include <rclcpp/logging.hpp>
#include <rclcpp/time.hpp>
#include <thread>

#include "process_msgs/msg/process_heartbeat.hpp"

using namespace std::chrono_literals;
using namespace std::placeholders;

namespace basin::launch_core {
LaunchNode::LaunchNode(const rclcpp::NodeOptions& options) :
    basin::node_core::NodeCore("launch_node", "", options) {
    // Load launch params
    params_ = std::make_unique<LaunchNodeParams>(this);

    // init the interpreters
    interpreter_ = std::make_unique<LaunchInterpreter>();

    // init the name
    controller_name_ = getHostname();
    // sanitize the name to remove any unwanted characters
    std::replace(controller_name_.begin(), controller_name_.end(), '-', '_');
    if (!params_->subsystem_postfix.empty()) {
        controller_name_ += "-" + params_->subsystem_postfix;
    }

    // Load the daemon launch file first. This is intentionally first as the interpreter will reset
    // to the active file when it loads the application launch filr
    if (!params_->daemon_file.empty()) {
        RCLCPP_INFO(get_logger(), "Loading daemon launch file %s", params_->daemon_file.c_str());
        loadLaunchFile(params_->daemon_file, params_->daemon_package, true);
    }

    // Load the default launch file
    if (!params_->launch_file.empty()) {
        loadLaunchFile(params_->launch_file, params_->launch_package, false);
    }

    // Prepare the ROS contexts for monitoring
    tick_timer_ = create_wall_timer(500ms, std::bind(&LaunchNode::onTick, this));
    process_list_pub_ =
        create_publisher<process_msgs::msg::ProcessList>("/state/launch/process_list", rclcpp::SystemDefaultsQoS());
    sys_info_pub_ =
        create_publisher<process_msgs::msg::SystemInfo>("/state/launch/sys_info", rclcpp::SystemDefaultsQoS());
    process_heartbeat_sub_ = create_subscription<process_msgs::msg::ProcessHeartbeat>(
        "/state/process_heartbeat", rclcpp::QoS(50).reliable(),
        std::bind(&LaunchNode::heartbeatCallback, this, _1));
    request_service_ = create_service<process_msgs::srv::RemoteRequest>(
        "/state/launch/" + controller_name_ + "/request",
        std::bind(&LaunchNode::requestCallback, this, _1, _2));

    // ignore child signals
    basin::process::ProcessController::setIgnoreSigchild(true);

    // Launch the daemon processes
    for (ControlledProcess& process : daemon_controllers_) {
        process.startProcess();

        ProcessStats stats = process.getStats(0.0);

        if (stats.status == ProcessStatus::STARTING) {
            RCLCPP_INFO_STREAM(get_logger(), "Started " << process.getName() << " with PID: " << stats.local_pid
                                                        << " with output file: " << process.getOutputLogPath());
        } else {
            RCLCPP_WARN_STREAM(get_logger(), "Failed to start " << process.getName() << ": " << process.getLastError());
        }
    }

    // As a last step, activate the launch
    startLaunch();
}

LaunchNode::~LaunchNode() { shutdown(); }

bool LaunchNode::loadLaunchFile(const std::string& launch_file, const std::string& launch_package,
                                          bool is_daemon) {
    RCLCPP_INFO_STREAM(get_logger(), "Loading launch file " << launch_file);

    int daemon_offset = 0;
    if (!is_daemon) {
        daemon_offset = daemon_controllers_.size();
    }

    // load the interpreter
    try {
        if (launch_package.empty()) {
            interpreter_->loadDescription(launch_file, daemon_offset);
        } else {
            interpreter_->loadDescription(launch_package, launch_file, daemon_offset);
        }
    } catch (const std::runtime_error& e) {
        RCLCPP_ERROR_STREAM(get_logger(), "Failed to load " << launch_file << " for reason: " << e.what());
        return false;
    } catch (const std::invalid_argument& e) {
        RCLCPP_ERROR_STREAM(get_logger(), "Failed to load " << launch_file << " for reason: " << e.what());
        return false;
    } catch (const ament_index_cpp::PackageNotFoundError& e) {
        RCLCPP_ERROR_STREAM(get_logger(), "Failed to load " << launch_file << " for reason: " << e.what());
        return false;
    }

    // Extract executable info
    std::vector<ProcessDescription> descriptions = interpreter_->getProcessDescription();

    // load the process controllers to start
    if (is_daemon) {
        daemon_controllers_.clear();
        for (const ProcessDescription& description : descriptions) {
            RCLCPP_INFO_STREAM(get_logger(), "Loaded daemon process: " << description.process_name);
            daemon_controllers_.emplace_back(description);
        }
    } else {
        process_controllers_.clear();
        for (const ProcessDescription& description : descriptions) {
            process_controllers_.emplace_back(description);
            RCLCPP_INFO_STREAM(get_logger(), "Loaded process: " << description.process_name);
        }
    }

    return true;
}

void LaunchNode::startLaunch() {
    for (ControlledProcess& process : process_controllers_) {
        process.startProcess();

        ProcessStats stats = process.getStats(0.0);

        if (stats.status == ProcessStatus::STARTING) {
            RCLCPP_INFO_STREAM(get_logger(), "Started " << process.getName() << " with PID: " << stats.local_pid
                                                        << " with output file: " << process.getOutputLogPath());
        } else {
            RCLCPP_WARN_STREAM(get_logger(), "Failed to start " << process.getName() << ": " << process.getLastError());
        }
    }
}

void LaunchNode::stopLaunch() {
    for (ControlledProcess& process : process_controllers_) {
        process.stopProcess();
    }
}

bool LaunchNode::isLaunchRunning() {
    bool processes_running = false;
    for (ControlledProcess& process : process_controllers_) {
        // verify it is either inactive or dead
        processes_running |= process.isRunning(0.0);
    }

    // Intentionally ignore daemon processes from running

    return processes_running;
}

process_msgs::msg::ProcessInfo LaunchNode::checkProcess(ControlledProcess& process, double watchdog_time,
                                                                  double cpu_total_delta) {
    process_msgs::msg::ProcessInfo info;
    ProcessStats status = process.getStats(watchdog_time);

    info.local_pid = static_cast<int32_t>(status.local_pid);
    info.process_cpu_pct = status.cpu_avg_delta / cpu_total_delta;
    info.process_memory = status.memory_usage;
    info.process_id = process.getId();
    info.process_name = process.getName();
    info.process_status = static_cast<uint8_t>(status.status);
    info.restart_counter = status.restart_counter;

    if (status.status == ProcessStatus::TIMEOUT) {
        RCLCPP_WARN_STREAM_THROTTLE(
            get_logger(), *get_clock(), 1000,
            process.getName() << " Timed out after " << params_->watchdog_delay_s << "s. process will now restart");
    }

    // Determine if the process needs an autorestart
    if (status.status == ProcessStatus::DEAD && status.restart_counter < params_->num_restarts) {
        process.startProcess();

        ProcessStats stats = process.getStats(0.0);
        if (stats.status == ProcessStatus::STARTING) {
            RCLCPP_WARN_STREAM(get_logger(), "Restarted dead " << process.getName() << " with PID: " << stats.local_pid
                                                               << " with output file: " << process.getOutputLogPath());
        } else {
            RCLCPP_WARN_STREAM(get_logger(),
                               "Failed to restart " << process.getName() << ": " << process.getLastError());
        }
    } else if (status.status == ProcessStatus::DEAD) {
        RCLCPP_ERROR_STREAM_THROTTLE(
            get_logger(), *get_clock(), 10000,
            process.getName() << " died " << status.restart_counter
                              << " times! Last output is located in output file: " << process.getOutputLogPath());
    } else {
        // nominal case, do nothing
    }

    return info;
}

void LaunchNode::onTick() {
    // record the starting time of the loop
    rclcpp::Time now = get_clock()->now();
    double watchdog_time = now.seconds();

    // get the curent system info
    system_monitor_.readStat();
    double cpu_total_delta = system_monitor_.getCpuTotalAvgDelta();

    // make the process listing message
    process_msgs::msg::ProcessList process_list;
    process_list.system_name = controller_name_;
    process_list.header.stamp = now;

    // Check on the daemon processes
    for (ControlledProcess& process : daemon_controllers_) {
        process_list.processes.push_back(checkProcess(process, watchdog_time, cpu_total_delta));
    }

    if (interpreter_) {
        process_list.active_config = interpreter_->getLaunchPath().string();
    }

    // check processes that are running here
    for (ControlledProcess& process : process_controllers_) {
        process_list.processes.push_back(checkProcess(process, watchdog_time, cpu_total_delta));
    }

    // Send the status message
    process_list_pub_->publish(process_list);

    // Build and publish system info
    process_msgs::msg::SystemInfo sys_info;
    sys_info.header.stamp = now;
    sys_info.system_name = controller_name_;
    
    for (double v : system_monitor_.getCpuPerCoreUser()) {
        sys_info.per_core_cpu_user.push_back(static_cast<float>(v));
    }
    for (double v : system_monitor_.getCpuPerCoreSys()) {
        sys_info.per_core_cpu_sys.push_back(static_cast<float>(v));
    }
    sys_info.used_memory = system_monitor_.getUsedMem();
    sys_info.free_memory = system_monitor_.getAvailableMem();
    sys_info_pub_->publish(sys_info);
}

void LaunchNode::shutdown() {
    // record the starting time of the loop
    rclcpp::Time now = get_clock()->now();
    double watchdog_time = now.seconds();

    RCLCPP_INFO_STREAM(get_logger(), "Shutting down " << process_controllers_.size() << " processes");

    // Need to use a more aggressive stop here
    bool processes_running = true;
    while (processes_running) {
        // try to invalidate the loop
        processes_running = false;
        for (ControlledProcess& process : process_controllers_) {
            // verify it is either inactive or dead
            bool process_active = process.isRunning(watchdog_time, true);
            if (process_active) {
                RCLCPP_INFO_STREAM(get_logger(), "Shutting down " << process.getName());

                // Request the process to stop
                process.stopProcess();
            }

            processes_running |= process_active;
        }

        for (ControlledProcess& process : daemon_controllers_) {
            // verify it is either inactive or dead
            bool process_active = process.isRunning(watchdog_time, true);
            if (process_active) {
                RCLCPP_INFO_STREAM(get_logger(), "Shutting down " << process.getName());

                // Request the process to stop
                process.stopProcess();
            }

            processes_running |= process_active;
        }

        std::this_thread::sleep_for(500ms);
    }

    basin::process::ProcessController::setIgnoreSigchild(false);
}

void LaunchNode::processHeartbeat(std::vector<ControlledProcess>& processes,
                                            const process_msgs::msg::ProcessHeartbeat::ConstSharedPtr& msg) {
    for (ControlledProcess& process : processes) {
        if (process.getName() == msg->process_name) {
            // convert the message time
            rclcpp::Time msg_time = msg->header.stamp;

            // MODE_STARTING = 0, MODE_RUNNING = 1
            if (msg->mode == process_msgs::msg::ProcessHeartbeat::MODE_STARTING) {
                // Check if process has already entered running state
                bool was_running = process.transitionBackToStarting();

                if (was_running) {
                    RCLCPP_WARN_STREAM(
                        get_logger(),
                        "Process '"
                            << process.getName() << "' transitioned from RUNNING back to STARTING state. "
                            << "This may indicate the process reinitialized or experienced an internal restart.");
                }
            } else if (msg->mode == process_msgs::msg::ProcessHeartbeat::MODE_RUNNING) {
                // Only feed the watchdog if the process has entered a running state
                process.feedWatchdog(msg_time.seconds() + params_->watchdog_delay_s);
            }
            break;
        }
    }
}

void LaunchNode::heartbeatCallback(process_msgs::msg::ProcessHeartbeat::ConstSharedPtr msg) {
    // Check application processes
    processHeartbeat(process_controllers_, msg);

    // Check daemon processes
    processHeartbeat(daemon_controllers_, msg);
}

void LaunchNode::requestCallback(process_msgs::srv::RemoteRequest::Request::SharedPtr req,
                                           process_msgs::srv::RemoteRequest::Response::SharedPtr resp) {
    // Investigate the request option
    if (req->request_id == req->REQUEST_NONE) {
        resp->resp_code = resp->RESPONSE_OK;
        resp->response_msg = "OK";
    } else if (req->request_id == req->REQUEST_LOAD_CONFIG) {
        // If we are already running, this is an immediate fail
        if (!isLaunchRunning()) {
            std::string launch_file = interpreter_->getLaunchPath().string();
            if (!req->scenario_argument.empty()) {
                launch_file = req->scenario_argument;
            }

            // we can configure and return status
            bool configured = loadLaunchFile(launch_file, params_->launch_package, false);

            if (configured) {
                resp->resp_code = resp->RESPONSE_OK;
                resp->response_msg = "Configured for new launch";
            } else {
                resp->resp_code = resp->RESPONSE_UNKNOWN_FAIL;
                resp->response_msg = "Failed to load configuration";
            }

        } else {
            // error and indicate we need a shutdown first
            resp->resp_code = resp->RESPONSE_SYSTEM_ACTIVE;
            resp->response_msg = "Must shutdown before configure!";
        }
    } else if (req->request_id == req->REQUEST_START) {
        // make sure the process exists first
        bool found_match = false;
        for (ControlledProcess& process : process_controllers_) {
            // If we have a match apply the request
            if (process.getId() == req->id_argument) {
                found_match = true;
                process.startProcess();

                ProcessStats stats = process.getStats(0.0);
                RCLCPP_INFO_STREAM(get_logger(), "Started " << process.getName() << " with PID: " << stats.local_pid
                                                            << " with output file: " << process.getOutputLogPath());

                resp->resp_code = resp->RESPONSE_OK;
                resp->response_msg = "Started process";
            }
        }

        // Also check daemon processes if not found
        if (!found_match) {
            for (ControlledProcess& process : daemon_controllers_) {
                if (process.getId() == req->id_argument) {
                    found_match = true;
                    process.startProcess();

                    ProcessStats stats = process.getStats(0.0);
                    RCLCPP_INFO_STREAM(get_logger(), "Started daemon " << process.getName() << " with PID: " << stats.local_pid
                                                                << " with output file: " << process.getOutputLogPath());

                    resp->resp_code = resp->RESPONSE_OK;
                    resp->response_msg = "Started daemon process";
                }
            }
        }

        if (!found_match) {
            resp->resp_code = resp->RESPONSE_INVALID_PROCESS_ID;
            resp->response_msg = "Unkown launch ID";
        }
    } else if (req->request_id == req->REQUEST_STOP) {
        // make sure the process exists first
        bool found_match = false;
        for (ControlledProcess& process : process_controllers_) {
            // If we have a match apply the request
            if (process.getId() == req->id_argument) {
                found_match = true;

                ProcessStats stats = process.getStats(0.0);
                RCLCPP_INFO_STREAM(get_logger(), "Stopping " << process.getName() << " with PID: " << stats.local_pid);

                process.stopProcess();

                resp->resp_code = resp->RESPONSE_OK;
                resp->response_msg = "Stopped process";
            }
        }

        // Also check daemon processes if not found
        if (!found_match) {
            for (ControlledProcess& process : daemon_controllers_) {
                if (process.getId() == req->id_argument) {
                    found_match = true;

                    ProcessStats stats = process.getStats(0.0);
                    RCLCPP_INFO_STREAM(get_logger(), "Stopping daemon " << process.getName() << " with PID: " << stats.local_pid);

                    process.stopProcess();

                    resp->resp_code = resp->RESPONSE_OK;
                    resp->response_msg = "Stopped daemon process";
                }
            }
        }

        if (!found_match) {
            resp->resp_code = resp->RESPONSE_INVALID_PROCESS_ID;
            resp->response_msg = "Unkown launch ID";
        }
    } else if (req->request_id == req->REQUEST_RESTART) {
        // make sure the process exists first
        bool found_match = false;
        for (ControlledProcess& process : process_controllers_) {
            // If we have a match apply the request
            if (process.getId() == req->id_argument) {
                found_match = true;

                process.restartProcess();

                ProcessStats stats = process.getStats(0.0);
                RCLCPP_INFO_STREAM(get_logger(), "Restarted " << process.getName() << " with PID: " << stats.local_pid
                                                              << " with output file: " << process.getOutputLogPath());

                resp->resp_code = resp->RESPONSE_OK;
                resp->response_msg = "Restarted process";
            }
        }

        // Also check daemon processes if not found
        if (!found_match) {
            for (ControlledProcess& process : daemon_controllers_) {
                if (process.getId() == req->id_argument) {
                    found_match = true;

                    process.restartProcess();

                    ProcessStats stats = process.getStats(0.0);
                    RCLCPP_INFO_STREAM(get_logger(), "Restarted daemon " << process.getName() << " with PID: " << stats.local_pid
                                                                  << " with output file: " << process.getOutputLogPath());

                    resp->resp_code = resp->RESPONSE_OK;
                    resp->response_msg = "Restarted daemon process";
                }
            }
        }

        if (!found_match) {
            resp->resp_code = resp->RESPONSE_INVALID_PROCESS_ID;
            resp->response_msg = "Unkown launch ID";
        }
    } else if (req->request_id == req->REQUEST_LAUNCH) {
        if (!isLaunchRunning()) {
            RCLCPP_INFO_STREAM(get_logger(), "Starting launch " << interpreter_->getLaunchPath());

            startLaunch();

            resp->resp_code = resp->RESPONSE_OK;
            resp->response_msg = "Started launch";
        } else {
            resp->resp_code = resp->RESPONSE_SYSTEM_ACTIVE;
            resp->response_msg = "Launch already active";
        }
    } else if (req->request_id == req->REQUEST_SHUTDOWN) {
        if (isLaunchRunning()) {
            RCLCPP_INFO_STREAM(get_logger(), "Stopping launch " << interpreter_->getLaunchPath());

            stopLaunch();

            resp->resp_code = resp->RESPONSE_OK;
            resp->response_msg = "Stopped launch";
        } else {
            resp->resp_code = resp->RESPONSE_SYSTEM_SHUTDOWN;
            resp->response_msg = "Launch already shutdown";
        }
    } else if (req->request_id == req->REQUEST_HOTLOAD) {
        RCLCPP_INFO_STREAM(get_logger(), "Hot-reloading launch " << interpreter_->getLaunchPath());

        // Stop the launch if it's running
        if (isLaunchRunning()) {
            RCLCPP_INFO(get_logger(), "Stopping launch for hot-reload");
            stopLaunch();
        }

        rclcpp::Time start = get_clock()->now();

        // need to wait for processes to shut down
        while (isLaunchRunning() && (get_clock()->now() - start < rclcpp::Duration(std::chrono::seconds(5)))) {
            // record the starting time of the loop
            rclcpp::Time now = get_clock()->now();
            double watchdog_time = now.seconds();

            // force poll process status
            for (auto& process : process_controllers_) {
                process.getStats(watchdog_time);
            }

            RCLCPP_INFO_THROTTLE(get_logger(), *get_clock(), 500, "Waiting for nodes to shut down");
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        // Reload the current launch configuration
        std::string launch_file = interpreter_->getLaunchPath().string();
        bool reloaded = loadLaunchFile(launch_file, params_->launch_package, false);

        if (!reloaded) {
            resp->resp_code = resp->RESPONSE_UNKNOWN_FAIL;
            resp->response_msg = "Failed to reload configuration";
            return;
        }

        // Start the launch back up
        startLaunch();

        resp->resp_code = resp->RESPONSE_OK;
        resp->response_msg = "Hot-reloaded launch successfully";
    } else {
        resp->resp_code = resp->RESPONSE_REQ_INVALID;
        resp->response_msg = "Invalid request type";
    }
}

}  // namespace basin::launch_core
