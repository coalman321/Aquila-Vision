#include "launch_core/controlled_process.hpp"

namespace basin::launch_core {

ControlledProcess::ControlledProcess(const ProcessDescription& description) :
    description_(description),
    next_timeout_(0.0),
    controller_(description.exe_path, description.args, description.process_name, { "/dev/shm" },
                description.scheduling),
    stop_level_(StopLevel::NO_STOP),
    stats_{} {}

void ControlledProcess::feedWatchdog(double next_timeout) {
    next_timeout_ = next_timeout;

    // Move the process into running the first time the watchdog is fed for it
    if (stats_.status == ProcessStatus::STARTING) {
        stats_.status = ProcessStatus::RUNNING;
    }
}

bool ControlledProcess::transitionBackToStarting() {
    // If currently running, transition back to starting and return true
    if (stats_.status == ProcessStatus::RUNNING) {
        stats_.status = ProcessStatus::STARTING;
        return true;
    }
    return false;
}

ProcessStats ControlledProcess::getStats(double current_time) {
    updateStatus(current_time);
    return stats_;
}

bool ControlledProcess::isRunning(double current_time, bool update_watchdog) {
    if (update_watchdog) {
        updateStatus(current_time);
    }

    return stats_.status != ProcessStatus::DEAD && stats_.status != ProcessStatus::STOPPED;
}

void ControlledProcess::startProcess() {
    // need to guard against already running
    if (!isRunning(0.0)) {
        try {
            controller_.launchProcess();
            stats_.status = ProcessStatus::STARTING;
            last_error_.clear();
        } catch (const std::runtime_error& e) {
            // A failed launch counts as a death so the normal restart handling applies
            stats_.status = ProcessStatus::DEAD;
            last_error_ = e.what();
        }
        stop_level_ = StopLevel::NO_STOP;
        next_timeout_ = 0.0;
        stats_.restart_counter++;
        cpu_history_.clear();
    }
}

void ControlledProcess::stopProcess() {
    // need to guard against already stopped
    if (isRunning(0.0)) {
        stats_.status = ProcessStatus::STOPPING;
        stats_.restart_counter = 0;
        stop_level_ = StopLevel::INTERRUPT;
    }
}

void ControlledProcess::restartProcess() {
    if (isRunning(0.0)) {
        stats_.status = ProcessStatus::RESTARTING;
        stop_level_ = StopLevel::INTERRUPT;
    } else {
        startProcess();
    }
}

void ControlledProcess::handleSignal() {
    basin::process::SignalType signal;
    if (stop_level_ == StopLevel::INTERRUPT) {
        signal = basin::process::SignalType::INTERRUPT;
        stop_level_ = StopLevel::TERMINATE;
    } else if (stop_level_ == StopLevel::TERMINATE) {
        signal = basin::process::SignalType::TERM;
        stop_level_ = StopLevel::KILL;
    } else if (stop_level_ == StopLevel::KILL) {
        signal = basin::process::SignalType::KILL;
    } else {
        throw std::invalid_argument("Bad stop level passed to controlled process stop");
    }
    controller_.signalProccess(signal);
}

void ControlledProcess::updateStatus(double current_time) {
    // if we are in any running state pull status
    basin::process::ControlStatus process_status = controller_.getProcessStatus();
    stats_.local_pid = process_status.current_pid;
    stats_.cpu_usage = process_status.cpu_time_s;
    stats_.memory_usage = process_status.used_memory_bytes;

    // Compute the process average used
    double avg_cpu_delta = 0.0;
    if (process_status.process_alive) {
        // add the new CPU time to the history
        cpu_history_.push_back(process_status.cpu_time_s);
        if (cpu_history_.size() > 10) {
            cpu_history_.pop_front();
        }

        // Compute the average delta from the current history
        for (size_t i = 0; i < cpu_history_.size() - 1; i++) {
            avg_cpu_delta += cpu_history_.at(i) - cpu_history_.at(i + 1);
        }
        avg_cpu_delta /= static_cast<double>(cpu_history_.size());
    }
    stats_.cpu_avg_delta = avg_cpu_delta;

    if (stats_.status == ProcessStatus::RUNNING && current_time > next_timeout_) {
        // If we are running and the watchdog wasn't fed. start the shutdown
        stats_.status = ProcessStatus::TIMEOUT;
        stop_level_ = StopLevel::INTERRUPT;
    } else if (stats_.status == ProcessStatus::STARTING || stats_.status == ProcessStatus::RUNNING
               || stats_.status == ProcessStatus::TIMEOUT) {
        // if we are currently in a running state leave to dead if the process dies
        if (!process_status.process_alive) {
            stats_.status = ProcessStatus::DEAD;
        }
    } else if (stats_.status == ProcessStatus::STOPPING) {
        // if we are being commanded to shut down enter stopped when the process dies
        if (!process_status.process_alive) {
            stats_.status = ProcessStatus::STOPPED;
        }
    } else if (stats_.status == ProcessStatus::RESTARTING) {
        // if we are restarting, watch for the process to stop the re-launch
        if (!process_status.process_alive) {
            stats_.status = ProcessStatus::STOPPED;
            startProcess();
        }
    }

    // If we have an active stop level, handle it
    if (stop_level_ != StopLevel::NO_STOP) {
        handleSignal();
    }
}

}  // namespace basin::launch_core
