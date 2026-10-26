#include "launch_core/controlled_process.hpp"

// Main libs
#include <gtest/gtest.h>

#include <process_lib/process_controller.hpp>

// STL libs
#include <chrono>
#include <iostream>
#include <thread>

using namespace std::chrono_literals;
using namespace basin::launch_core;

TEST(ControlledProcessTest, test_create_process) {
    // Setup the description
    ProcessDescription desc;
    desc.exe_path = "/bin/ls";
    desc.args = { "-a" };
    desc.process_name = "ls";
    desc.id = 10;

    // setup the process
    ControlledProcess proc(desc);

    // Verify the process info
    ASSERT_EQ(proc.getId(), desc.id);
    ASSERT_EQ(proc.getName(), desc.process_name);
    ASSERT_FALSE(proc.isRunning(0.0));

    // check status of the process
    proc.feedWatchdog(10.0);
    ProcessStats stats = proc.getStats(1.0);
    ASSERT_EQ(stats.local_pid, -1);
    ASSERT_EQ(stats.memory_usage, 0);
    ASSERT_EQ(stats.cpu_usage, 0.0);
    ASSERT_EQ(stats.status, ProcessStatus::STOPPED);
}

TEST(ControlledProcessTest, test_launch_process) {
    // Setup the description. The process lives long enough to be observed running, but exits on its own
    ProcessDescription desc;
    desc.exe_path = "/bin/sleep";
    desc.args = { "0.3" };
    desc.process_name = "sleep";
    desc.id = 10;

    basin::process::ProcessController::setIgnoreSigchild(true);

    // setup the process
    ControlledProcess proc(desc);

    // launch the process
    proc.startProcess();

    // check status of the process
    ProcessStats stats = proc.getStats(1.0);
    ASSERT_TRUE(proc.isRunning(0.0, true));
    ASSERT_NE(stats.local_pid, -1);
    ASSERT_EQ(stats.status, ProcessStatus::STARTING);

    // Wait for the process to die, polling rather than assuming how long the launch takes
    auto deadline = std::chrono::steady_clock::now() + 5s;
    while (proc.isRunning(0.0, true) && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(10ms);
    }

    // check status of the process
    stats = proc.getStats(2.0);
    ASSERT_FALSE(proc.isRunning(0.0, true));
    ASSERT_EQ(stats.local_pid, -1);
    ASSERT_EQ(stats.status, ProcessStatus::DEAD);

    basin::process::ProcessController::setIgnoreSigchild(false);
}

TEST(ControlledProcessTest, test_control_process) {
    // Setup the description
    ProcessDescription desc;
    desc.exe_path = "/bin/sh";
    desc.args = { "-c", "while :; do echo 'Press <CTRL+C> to exit.'; sleep 1; done" };
    desc.process_name = "sh";
    desc.id = 10;

    basin::process::ProcessController::setIgnoreSigchild(true);

    // setup the process
    ControlledProcess proc(desc);

    // launch the process
    proc.startProcess();

    // check status of the process
    ProcessStats stats = proc.getStats(1.0);
    pid_t launched_pid = stats.local_pid;
    EXPECT_TRUE(proc.isRunning(0.0, true));
    EXPECT_NE(launched_pid, -1);
    EXPECT_EQ(stats.status, ProcessStatus::STARTING);

    // let the process run for a bit
    std::this_thread::sleep_for(1500ms);

    // check status of the process
    stats = proc.getStats(2.0);
    EXPECT_TRUE(proc.isRunning(0.0, true));
    EXPECT_EQ(stats.local_pid, launched_pid);
    EXPECT_EQ(stats.status, ProcessStatus::STARTING);

    // shut it down
    proc.stopProcess();
    proc.getStats(3.0);
    proc.getStats(4.0);

    // let the process run for a bit
    std::this_thread::sleep_for(100ms);

    basin::process::ProcessController::setIgnoreSigchild(false);

    stats = proc.getStats(5.0);
    ASSERT_FALSE(proc.isRunning(0.0, true));
    ASSERT_EQ(stats.local_pid, -1);
    ASSERT_EQ(stats.status, ProcessStatus::STOPPED);
}

TEST(ControlledProcessTest, test_failed_launch) {
    // An executable that doesn't exist fails in the child, which must be reported rather than thrown
    ProcessDescription desc;
    desc.exe_path = "/does/not/exist";
    desc.process_name = "missing";
    desc.id = 10;

    ControlledProcess proc(desc);
    ASSERT_NO_THROW(proc.startProcess());

    ProcessStats stats = proc.getStats(0.0);
    EXPECT_EQ(stats.status, ProcessStatus::DEAD);
    EXPECT_EQ(stats.local_pid, -1);
    EXPECT_FALSE(proc.isRunning(0.0));
    EXPECT_NE(proc.getLastError().find("Failed to execute"), std::string::npos) << proc.getLastError();
}
