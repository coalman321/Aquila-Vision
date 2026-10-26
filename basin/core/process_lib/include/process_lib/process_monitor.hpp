
#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>

namespace basin::process {

static constexpr pid_t INACTIVE_PID = -1;

class ProcessMonitor {
   public:
    ProcessMonitor() = default;

    ProcessMonitor(pid_t pid) : monitored_pid_(pid){};

    /// @brief Updates the current process status information
    void readStat();

    /// @brief Determines if the monitored process has exited from the kernel
    /// @details This is updated after the readStat call has been made
    /// @return True if the process is dead.
    bool isDead() { return monitored_pid_ == INACTIVE_PID; }

    /// @brief Gets the total CPU time the process has taken since start
    /// @details This is updated after the readStat call has been made
    /// @return The total cpu time used by the process in seconds
    double getTotalCpuTimeSec() { return (user_time_ + sys_time_); }

    /// @brief Gets the cpu time the process has spent in "user" mode
    /// @details This is updated after the readStat call has been made
    /// @return The user mode cpu time in seconds for the process
    double getUserCpuTimeSec() { return user_time_; }

    /// @brief Gets the cpu time the process has spent in "system" mode
    /// @details This is updated after the readStat call has been made
    /// @return The system mode cpu time in seconds for the process
    double getSysCpuTimeSec() { return sys_time_; }

    /// @brief Gets the amount of memory used by the process in bytes
    /// @details This is updated after the readStat call has been made
    /// @return The number of bytes allocated to the process
    double getMemoryUsageBytes() { return vm_size_bytes_; }

    /// @brief Gets the name of the process as reported by the kernel
    /// @details This is updated after the readStat call has been made
    /// @return The name of the process as reported by the kernel
    std::string getProcessName() { return process_name_; }

   protected:
    void setPID(pid_t pid);
    pid_t getPID() { return monitored_pid_; }

   private:
    // Active process status fields
    double user_time_{ 0.0 };         ///< The time in seconds the process has spent in user mode
    double sys_time_{ 0.0 };          ///< The time in seconds the process has spent in sys mode
    uint32_t vm_size_bytes_{ 0 };     ///< Number of bytes the program occupies in memory
    std::string process_name_{ "" };  ///< Name of the process are reported by the kernel

    // Key process information
    pid_t monitored_pid_{ INACTIVE_PID };  ///< PID of the monitored process
};

}  // namespace basin::process
