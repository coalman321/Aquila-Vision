#include "process_lib/process_controller.hpp"

#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <initializer_list>
#include <stdexcept>

namespace basin::process {

namespace {
int toSignalNumber(const SignalType signal) {
    switch (signal) {
        case SignalType::INTERRUPT:
            return SIGINT;
        case SignalType::TERM:
            return SIGTERM;
        case SignalType::KILL:
            return SIGKILL;
        case SignalType::ABORT:
            return SIGABRT;
        case SignalType::USER1:
            return SIGUSR1;
        case SignalType::USER2:
            return SIGUSR2;
    }
    throw std::invalid_argument("Unimplemented signal type");
}

std::shared_ptr<int> ownFd(int fd) {
    return std::shared_ptr<int>(new int(fd), [](int *owned) {
        close(*owned);
        delete owned;
    });
}
}  // namespace

ProcessController::ProcessController(std::filesystem::path executable, const std::vector<std::string> &args,
                                     const std::string &process_name, std::filesystem::path log_dir,
                                     std::optional<SchedulingConfig> scheduling) :
    scheduling_(scheduling) {
    setPID(INACTIVE_PID);

    executable_ = executable;
    program_args_ = args;
    process_name_ = process_name;

    // Sanitize the process name used for the output file
    if (process_name_.at(0) == '/') {
        process_name_ = process_name_.substr(1);
    }
    std::replace(process_name_.begin(), process_name_.end(), '/', '_');

    // Build the output file name
    log_path_ = log_dir / (process_name_ + ".out");
}

std::filesystem::path ProcessController::getOutputFile() { return log_path_; }

void ProcessController::setParentDeathSignal(SignalType signal) { parent_death_signal_ = toSignalNumber(signal); }

namespace {

/// The step of the child's launch sequence that failed, reported back to the parent
enum class LaunchStage : int {
    OPEN_LOG,
    SET_SCHEDULER,
    SET_NICE,
    EXEC,
};

struct LaunchError {
    LaunchStage stage;
    int error_number;
};

/// Reports a failed launch step to the parent and exits the child.
/// @details Only async-signal-safe calls are allowed here, the child of a multithreaded parent must not allocate,
/// throw or unwind back into the parent's code.
[[noreturn]] void failChild(int error_pipe, LaunchStage stage) {
    LaunchError error{ stage, errno };
    ssize_t written = write(error_pipe, &error, sizeof(error));
    (void)written;
    _exit(127);
}

std::string describeLaunchError(const LaunchError &error) {
    std::string step;
    switch (error.stage) {
        case LaunchStage::OPEN_LOG:
            step = "open the output file";
            break;
        case LaunchStage::SET_SCHEDULER:
            step = "set the scheduler policy";
            break;
        case LaunchStage::SET_NICE:
            step = "set the nice value";
            break;
        case LaunchStage::EXEC:
            step = "execute";
            break;
    }
    return "Failed to " + step + ": " + std::strerror(error.error_number);
}

void closeAll(std::initializer_list<int> fds) {
    for (int fd : fds) {
        if (fd >= 0) {
            close(fd);
        }
    }
}

}  // namespace

void ProcessController::launchProcess() {
    // Prepare everything the child needs before forking, the child of a multithreaded parent must not allocate
    std::filesystem::path out_file = getOutputFile();
    std::vector<const char *> c_args;
    c_args.push_back(executable_.c_str());
    std::string command_line = executable_.string();
    for (const std::string &arg : program_args_) {
        c_args.push_back(arg.c_str());
        command_line += " " + arg;
    }
    c_args.push_back(nullptr);
    command_line += "\n";

    // The child reports launch failures over this pipe. It closes on a successful exec, so EOF means success.
    int error_pipe[2];
    if (pipe2(error_pipe, O_CLOEXEC) != 0) {
        throw std::runtime_error("Failed to create launch pipe: " + std::string(std::strerror(errno)));
    }

    // In pipe mode the child writes into pipes the parent keeps the read ends of. O_CLOEXEC keeps
    // every end out of the exec'd program except the two it dup2()s onto stdout and stderr.
    int out_pipe[2] = { -1, -1 };
    int err_pipe[2] = { -1, -1 };
    if (output_mode_ == OutputMode::PIPE) {
        if (pipe2(out_pipe, O_CLOEXEC) != 0) {
            int pipe_error = errno;
            closeAll({ error_pipe[0], error_pipe[1] });
            throw std::runtime_error("Failed to create stdout pipe: " + std::string(std::strerror(pipe_error)));
        }
        if (pipe2(err_pipe, O_CLOEXEC) != 0) {
            int pipe_error = errno;
            closeAll({ error_pipe[0], error_pipe[1], out_pipe[0], out_pipe[1] });
            throw std::runtime_error("Failed to create stderr pipe: " + std::string(std::strerror(pipe_error)));
        }
    }
    const pid_t parent_pid = getpid();

    // Fork into another process
    pid_t pid = fork();

    if (pid == 0) {
        // This becomes the child process
        close(error_pipe[0]);

        if (parent_death_signal_ != 0) {
            prctl(PR_SET_PDEATHSIG, parent_death_signal_);
            // The parent may already have exited before the prctl took effect
            if (getppid() != parent_pid) {
                _exit(1);
            }
        }

        if (output_mode_ == OutputMode::PIPE) {
            dup2(out_pipe[1], STDOUT_FILENO);
            dup2(err_pipe[1], STDERR_FILENO);
        } else {
            // open a new FD for a temp output file
            int fd = open(out_file.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
            if (fd < 0) {
                failChild(error_pipe[1], LaunchStage::OPEN_LOG);
            }

            // Move stdout and stderr to use the new FD
            dup2(fd, STDOUT_FILENO);
            dup2(fd, STDERR_FILENO);
            close(fd);

            // Record the process launch arguments
            ssize_t written = write(STDOUT_FILENO, command_line.c_str(), command_line.size());
            (void)written;
        }

        // Apply the scheduler settings before exec so every thread the process creates inherits them. Reset on fork
        // keeps the process' own children from inheriting a realtime policy.
        if (scheduling_.has_value()) {
            sched_param param{};
            param.sched_priority = scheduling_->priority;
            if (sched_setscheduler(0, scheduling_->policy | SCHED_RESET_ON_FORK, &param) != 0) {
                failChild(error_pipe[1], LaunchStage::SET_SCHEDULER);
            }
            if (scheduling_->nice.has_value() && setpriority(PRIO_PROCESS, 0, *scheduling_->nice) != 0) {
                failChild(error_pipe[1], LaunchStage::SET_NICE);
            }
        }

        // need to execv to fully become the child process, it only returns on failure
        execv(executable_.c_str(), const_cast<char *const *>(c_args.data()));
        failChild(error_pipe[1], LaunchStage::EXEC);

    } else if (pid > 0) {
        // This becomes the parent process
        closeAll({ error_pipe[1], out_pipe[1], err_pipe[1] });

        // Wait for the child to either exec (EOF) or report why it couldn't
        LaunchError error{};
        ssize_t bytes_read;
        do {
            bytes_read = read(error_pipe[0], &error, sizeof(error));
        } while (bytes_read < 0 && errno == EINTR);
        close(error_pipe[0]);

        if (bytes_read == sizeof(error)) {
            closeAll({ out_pipe[0], err_pipe[0] });
            // The child has exited, reap it if SIGCHLD isn't being ignored
            waitpid(pid, nullptr, 0);
            throw std::runtime_error(describeLaunchError(error) + " " + executable_.string());
        }

        setPID(pid);
        exit_code_ = -1;
        exit_signal_ = 0;
        if (output_mode_ == OutputMode::PIPE) {
            stdout_fd_ = ownFd(out_pipe[0]);
            stderr_fd_ = ownFd(err_pipe[0]);
        } else {
            stdout_fd_.reset();
            stderr_fd_.reset();
        }
    } else {
        // This is the case where the parent fails to fork
        int fork_error = errno;
        closeAll({ error_pipe[0], error_pipe[1], out_pipe[0], out_pipe[1], err_pipe[0], err_pipe[1] });
        throw std::runtime_error("Failed to launch child process: " + std::string(std::strerror(fork_error)));
    }
}

void ProcessController::signalProccess(const SignalType signal) {
    pid_t pid_to_signal = getPID();
    if (pid_to_signal <= 0) {
        return;
    }

    int result = kill(pid_to_signal, toSignalNumber(signal));
    if (result != 0) {
        throw std::runtime_error("Kill raised error " + std::string(strerror(errno)));
    }
}

ControlStatus ProcessController::getProcessStatus() {
    // Reap the process if it has exited, so it isn't counted alive as a zombie, and keep its exit status. With
    // SIGCHLD ignored the kernel has already reaped it, waitpid fails with ECHILD, and readStat finds it gone.
    pid_t pid = getPID();
    if (pid > 0) {
        int wait_status = 0;
        if (waitpid(pid, &wait_status, WNOHANG) == pid) {
            if (WIFEXITED(wait_status)) {
                exit_code_ = WEXITSTATUS(wait_status);
            } else if (WIFSIGNALED(wait_status)) {
                exit_signal_ = WTERMSIG(wait_status);
            }
            setPID(INACTIVE_PID);
        }
    }

    // query the status of the process
    readStat();

    // check the status of the process
    ControlStatus status;
    status.process_alive = !isDead();
    status.current_pid = getPID();
    status.exit_code = exit_code_;
    status.exit_signal = exit_signal_;
    if (status.process_alive) {
        // only check cpu and memory if running
        status.cpu_time_s = getTotalCpuTimeSec();
        status.used_memory_bytes = getMemoryUsageBytes();
    }

    return status;
}

void ProcessController::setIgnoreSigchild(bool want_ignore) {
    struct sigaction sigchld_ignore;
    sigchld_ignore.sa_handler = want_ignore ? SIG_IGN : SIG_DFL;
    sigchld_ignore.sa_flags = 0;  // or SA_RESTART
    sigemptyset(&sigchld_ignore.sa_mask);
    sigaction(SIGCHLD, &sigchld_ignore, NULL);
}

}  // namespace basin::process
