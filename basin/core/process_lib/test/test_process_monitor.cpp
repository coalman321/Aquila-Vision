#include <gtest/gtest.h>
#include <unistd.h>

#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>

#include "process_lib/process_monitor.hpp"

TEST(ProcessMonitorTest, test_get_process_status) {
    pid_t pid = getpid();
    basin::process::ProcessMonitor monitor(pid);

    monitor.readStat();

    // Read the expected name from /proc/self/comm (max 15 chars, no parens)
    std::string expected_name;
    std::ifstream comm("/proc/self/comm");
    std::getline(comm, expected_name);

    ASSERT_EQ(monitor.getProcessName(), expected_name);
    ASSERT_EQ(monitor.isDead(), false);
}
