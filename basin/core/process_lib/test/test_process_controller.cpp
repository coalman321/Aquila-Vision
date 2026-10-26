#include <gtest/gtest.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>

#include <chrono>
#include <string>
#include <thread>

#include "process_lib/process_controller.hpp"

using namespace std::chrono_literals;

TEST(ProcessControllerTest, test_launching_ephemeral_process) {
    // Disable sigchild so we dont have zombie PID's
    basin::process::ProcessController::setIgnoreSigchild(true);
    // launchProcess returns after exec, so the process needs to outlive the first status check
    basin::process::ProcessController shell_controller("/bin/sh", { "-c", "echo 'blah'; sleep 0.2" }, "shell_echo");

    ASSERT_NO_THROW(shell_controller.launchProcess());
    EXPECT_TRUE(shell_controller.getProcessStatus().process_alive);

    // wait for the process to exit on its own
    basin::process::ControlStatus status = shell_controller.getProcessStatus();
    for (int i = 0; i < 100 && status.process_alive; i++) {
        std::this_thread::sleep_for(20ms);
        status = shell_controller.getProcessStatus();
    }

    // re-enable sigchild
    basin::process::ProcessController::setIgnoreSigchild(false);

    ASSERT_FALSE(status.process_alive);
}

TEST(ProcessControllerTest, test_signaling_process) {
    // Ignore sigchild so we dont get zombies
    basin::process::ProcessController::setIgnoreSigchild(true);

    basin::process::ProcessController shell_controller(
        "/bin/sh", { "-c", "while :; do echo 'Press <CTRL+C> to exit.'; sleep 1; done" }, "shell/sleep");

    // launch the process
    ASSERT_NO_THROW(shell_controller.launchProcess());
    basin::process::ControlStatus status = shell_controller.getProcessStatus();
    pid_t launched_pid = status.current_pid;

    std::cout << "launched pid: " << launched_pid << std::endl;

    // give the process time to start
    std::this_thread::sleep_for(10ms);

    status = shell_controller.getProcessStatus();
    EXPECT_TRUE(status.process_alive);
    EXPECT_EQ(launched_pid, status.current_pid);

    // request the process to stop
    shell_controller.signalProccess(basin::process::SignalType::INTERRUPT);

    // Give it time to stop
    std::this_thread::sleep_for(1500ms);

    // verify process stopped
    status = shell_controller.getProcessStatus();

    // re-enable sigchild
    basin::process::ProcessController::setIgnoreSigchild(false);

    ASSERT_FALSE(status.process_alive);
}

namespace {

/// Reads `fd` to end-of-file.
std::string readAll(int fd) {
    std::string out;
    char buffer[256];
    ssize_t count;
    while ((count = read(fd, buffer, sizeof(buffer))) > 0) {
        out.append(buffer, static_cast<size_t>(count));
    }
    return out;
}

/// Polls the controller until its process has exited (and been reaped).
basin::process::ControlStatus waitForExit(basin::process::ProcessController& controller) {
    const auto deadline = std::chrono::steady_clock::now() + 5s;
    basin::process::ControlStatus status = controller.getProcessStatus();
    while (status.process_alive && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
        status = controller.getProcessStatus();
    }
    return status;
}

}  // namespace

TEST(ProcessControllerTest, test_pipe_mode_separates_stdout_and_stderr) {
    basin::process::ProcessController controller("/bin/sh", { "-c", "echo out; echo err >&2; exit 3" }, "shell_pipe");
    controller.setOutputMode(basin::process::OutputMode::PIPE);
    EXPECT_EQ(controller.getStdoutFd(), -1);

    ASSERT_NO_THROW(controller.launchProcess());
    ASSERT_GE(controller.getStdoutFd(), 0);
    ASSERT_GE(controller.getStderrFd(), 0);
    EXPECT_EQ(readAll(controller.getStdoutFd()), "out\n");
    EXPECT_EQ(readAll(controller.getStderrFd()), "err\n");

    basin::process::ControlStatus status = waitForExit(controller);
    EXPECT_FALSE(status.process_alive);
    EXPECT_EQ(status.exit_code, 3);
    EXPECT_EQ(status.exit_signal, 0);
}

TEST(ProcessControllerTest, test_reaps_and_reports_a_signalled_process) {
    // With SIGCHLD at its default the controller has to reap the process itself, or it would stay a zombie that
    // still counts as alive
    basin::process::ProcessController controller("/bin/sh", { "-c", "sleep 30" }, "shell_killed");
    ASSERT_NO_THROW(controller.launchProcess());
    EXPECT_TRUE(controller.getProcessStatus().process_alive);

    controller.signalProccess(basin::process::SignalType::KILL);
    basin::process::ControlStatus status = waitForExit(controller);
    EXPECT_FALSE(status.process_alive);
    EXPECT_EQ(status.exit_code, -1);
    EXPECT_EQ(status.exit_signal, SIGKILL);
}

TEST(ProcessControllerTest, test_unrunnable_executable_in_pipe_mode_throws) {
    // The launch fails in the parent rather than handing back pipes from a child that never ran
    basin::process::ProcessController controller("/nonexistent/program", {}, "missing");
    controller.setOutputMode(basin::process::OutputMode::PIPE);
    EXPECT_THROW(controller.launchProcess(), std::runtime_error);
    EXPECT_EQ(controller.getStdoutFd(), -1);
    EXPECT_EQ(controller.getStderrFd(), -1);
    EXPECT_FALSE(controller.getProcessStatus().process_alive);
}

TEST(ProcessControllerTest, test_log_file_mode_records_the_command_line) {
    basin::process::ProcessController controller("/bin/sh", { "-c", "echo hello" }, "shell_log_file",
                                                 std::filesystem::temp_directory_path());
    ASSERT_NO_THROW(controller.launchProcess());
    EXPECT_EQ(waitForExit(controller).exit_code, 0);

    FILE* file = std::fopen(controller.getOutputFile().c_str(), "r");
    ASSERT_NE(file, nullptr);
    std::string contents = readAll(fileno(file));
    std::fclose(file);
    EXPECT_EQ(contents, "/bin/sh -c echo hello\nhello\n");
}

TEST(ProcessControllerTest, test_scheduling_applied) {
    basin::process::ProcessController::setIgnoreSigchild(true);

    // SCHED_BATCH with a raised nice value needs no privileges
    basin::process::SchedulingConfig scheduling;
    scheduling.policy = SCHED_BATCH;
    scheduling.nice = 10;
    basin::process::ProcessController controller("/bin/sleep", { "5" }, "sched_sleep", "/dev/shm", scheduling);

    // launchProcess only returns once the child has exec'd, so the settings are already in place
    ASSERT_NO_THROW(controller.launchProcess());
    pid_t pid = controller.getProcessStatus().current_pid;
    ASSERT_GT(pid, 0);

    EXPECT_EQ(sched_getscheduler(pid) & ~SCHED_RESET_ON_FORK, SCHED_BATCH);
    errno = 0;
    EXPECT_EQ(getpriority(PRIO_PROCESS, pid), 10);
    EXPECT_EQ(errno, 0);

    controller.signalProccess(basin::process::SignalType::KILL);
    basin::process::ProcessController::setIgnoreSigchild(false);
}

TEST(ProcessControllerTest, test_invalid_scheduling_throws) {
    basin::process::ProcessController::setIgnoreSigchild(true);

    // SCHED_FIFO requires a priority of at least 1, so the kernel rejects this regardless of privileges
    basin::process::SchedulingConfig scheduling;
    scheduling.policy = SCHED_FIFO;
    scheduling.priority = 0;
    basin::process::ProcessController controller("/bin/sleep", { "5" }, "sched_invalid", "/dev/shm", scheduling);

    try {
        controller.launchProcess();
        FAIL() << "Expected the launch to fail";
    } catch (const std::runtime_error& e) {
        EXPECT_NE(std::string(e.what()).find("scheduler policy"), std::string::npos) << e.what();
    }
    EXPECT_FALSE(controller.getProcessStatus().process_alive);

    basin::process::ProcessController::setIgnoreSigchild(false);
}

TEST(ProcessControllerTest, test_missing_executable_throws) {
    basin::process::ProcessController::setIgnoreSigchild(true);

    basin::process::ProcessController controller("/does/not/exist", {}, "missing_exe");

    EXPECT_THROW(controller.launchProcess(), std::runtime_error);
    EXPECT_FALSE(controller.getProcessStatus().process_alive);

    basin::process::ProcessController::setIgnoreSigchild(false);
}
