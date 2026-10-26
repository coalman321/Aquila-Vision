#pragma once

#include <process_lib/process_controller.hpp>
#include <deque>

#include "launch_core/launch_interpreter.hpp"

namespace basin::launch_core {

enum ProcessStatus {
    ///< The process is currently not running (gracefully stopped or not yet run)
    STOPPED,

    ///< The process is currently starting and has not connected to the watchdog
    STARTING,

    ///< The process is currently running and has connected to the watchdog
    RUNNING,

    ///< The process has timed out form the watchdog as is being restarted
    TIMEOUT,

    ///< The process has entered a shuting down state and has at least be interrupted
    STOPPING,

    ///< The process has entered a restarting state
    RESTARTING,

    ///< The process died with an error code and is no longer able to be queued to restart
    DEAD,

    ///< The process is unable to be launched due to an error in the launch file or missing package
    UNKNOWN,
};

enum StopLevel {
    ///< No current stop level
    NO_STOP,

    ///< Interrupts the process
    INTERRUPT,

    ///< Requests process to terminate
    TERMINATE,

    ///< Requests the process to exit immediately
    KILL,
};

struct ProcessStats {
    ProcessStatus status{ ProcessStatus::STOPPED };

    pid_t local_pid{ -1 };

    int32_t restart_counter{ -1 };

    double cpu_usage{ 0.0 };

    double cpu_avg_delta{ 0.0 };

    uint64_t memory_usage{ 0 };
};

class ControlledProcess {
   public:
    ControlledProcess(const ProcessDescription& description);

    void startProcess();

    void stopProcess();

    void restartProcess();

    void feedWatchdog(double next_timeout);

    bool transitionBackToStarting();

    ProcessStats getStats(double current_time);

    bool isRunning(double current_time, bool update_watchdog = false);

    std::string getName() { return description_.process_name; }

    std::filesystem::path getOutputLogPath() { return controller_.getOutputFile(); }

    uint32_t getId() { return description_.id; }

    /// @brief Gets the reason the last start attempt failed
    /// @return The error message, empty if the last start succeeded
    std::string getLastError() { return last_error_; }

   private:
    void updateStatus(double current_time);

    void handleSignal();

    ///< Description for the controlled process including executable and args
    ProcessDescription description_;

    ///< Time at which the node will be considered timed out if in a running state
    double next_timeout_;

    ///< Process controller used for managing the controlled process
    basin::process::ProcessController controller_;

    ///< Current stop level for the process
    StopLevel stop_level_;

    ///< Current stats for the process
    ProcessStats stats_;

    ///< Stats history for the process
    std::deque<double> cpu_history_;

    ///< Reason the last start attempt failed, empty if it succeeded
    std::string last_error_;
};

}  // namespace basin::launch_core
