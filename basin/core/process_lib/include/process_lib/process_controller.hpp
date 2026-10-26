
#pragma once

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "process_lib/process_monitor.hpp"

namespace basin::process {

struct ControlStatus {
    bool process_alive{ false };        ///< flag for checking if the process is currently alive
    pid_t current_pid{ INACTIVE_PID };  ///< The current system PID of the process

    double cpu_time_s{ 0.0 };         ///< The amount of CPU time used in seconds by the process in sys & user mode
    uint64_t used_memory_bytes{ 0 };  ///< The bytes of memory used by the process

    /// Exit status of the last run, once getProcessStatus() has reaped it; -1 while running, if it was killed by a
    /// signal, or if SIGCHLD is ignored (see setIgnoreSigchild), in which case the kernel discards the status
    int exit_code{ -1 };
    int exit_signal{ 0 };  ///< The signal that killed the last run, if one did and it was reaped; otherwise 0
};

struct SchedulingConfig {
    int policy{ 0 };          ///< The kernel scheduler policy (SCHED_OTHER, SCHED_FIFO, ...)
    int priority{ 0 };        ///< The static priority, only non-zero for SCHED_FIFO and SCHED_RR
    std::optional<int> nice;  ///< The nice value for SCHED_OTHER and SCHED_BATCH, unset leaves it inherited
};

enum SignalType {
    INTERRUPT,
    KILL,
    ABORT,
    TERM,
    USER1,
    USER2,
};

/// @brief Where a controlled process's stdout and stderr go
enum class OutputMode {
    LOG_FILE,  ///< Both go to the output file (see getOutputFile()), headed by the command line. The default
    PIPE,      ///< Each goes to its own pipe, which the parent reads through getStdoutFd() / getStderrFd()
};

class ProcessController : public ProcessMonitor {
   public:
    /// @brief Creates a controlled process that a C++ program can harness. It also redirects the process stdout and
    /// stderr to a file in /dev/shm/
    /// @param executable The path to the process binary
    /// @param args The args to feed to the process
    /// @param process_name The name of the process to be used for the output file in /dev/shm
    /// @param log_dir The directory for log files (default: /dev/shm)
    /// @param scheduling The scheduler settings to apply to the process, unset inherits them from this process
    ProcessController(std::filesystem::path executable, const std::vector<std::string>& args,
                      const std::string& process_name, std::filesystem::path log_dir = { "/dev/shm" },
                      std::optional<SchedulingConfig> scheduling = std::nullopt);

    /// @brief Chooses where the process's stdout and stderr go, from the next launchProcess() on
    /// @param mode The output mode to use
    void setOutputMode(OutputMode mode) { output_mode_ = mode; }

    /// @brief Has the kernel send `signal` to the process if the parent goes first (PR_SET_PDEATHSIG), from the next
    /// launchProcess() on. Note the kernel ties this to the parent *thread* that launched the process, not the
    /// whole parent process: if that thread exits, the child gets the signal too
    /// @param signal The signal to send
    void setParentDeathSignal(SignalType signal);

    /// @brief Launches the process specified during construction
    /// @throws std::runtime_error if the process could not be forked, configured or executed
    void launchProcess();

    /// @brief Send a signal to the underlying controlled process
    /// @param type The type of signal to send to the process
    void signalProccess(const SignalType type);

    /// @brief Gets the current status of the controlled process
    /// @return The status of the controlled process
    ControlStatus getProcessStatus();

    /// @brief Gets the path to the console output file
    /// @return The fs path to the output file
    std::filesystem::path getOutputFile();

    /// @brief Gets the read end of the last launch's stdout / stderr pipe, in PIPE mode
    /// @details The controller owns the descriptors and closes them on the next launchProcess() or on destruction,
    /// so a reader must be done with them by then. They read end-of-file once the process (and anything it passed
    /// them to) has exited
    /// @return The file descriptor, or -1 if not in PIPE mode or not yet launched
    int getStdoutFd() const { return stdout_fd_ ? *stdout_fd_ : -1; }
    int getStderrFd() const { return stderr_fd_ ? *stderr_fd_ : -1; }

    /// @brief Helper function to apply to the parent to allow child processes to exit without the parent checking on
    /// them
    /// @param want_ignore True to force the current process to ignore the signals. false to require the signals
    static void setIgnoreSigchild(bool want_ignore);

   private:
    std::filesystem::path executable_;
    OutputMode output_mode_{ OutputMode::LOG_FILE };
    int parent_death_signal_{ 0 };
    // Shared so a copied controller closes each descriptor once
    std::shared_ptr<int> stdout_fd_;
    std::shared_ptr<int> stderr_fd_;
    int exit_code_{ -1 };
    int exit_signal_{ 0 };
    std::filesystem::path log_path_;
    std::vector<std::string> program_args_;
    std::string process_name_;
    std::optional<SchedulingConfig> scheduling_;
};
}  // namespace basin::process
