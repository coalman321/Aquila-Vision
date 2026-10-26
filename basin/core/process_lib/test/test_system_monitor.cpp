#include <gtest/gtest.h>

#include <iomanip>
#include <iostream>

#include "process_lib/system_monitor.hpp"

TEST(SystemMonitorTest, test_get_system_stats) {
    basin::process::SystemMonitor monitor;

    monitor.readStat();

    // std::cout << "System mem: " << monitor.getTotalMem() * 1e-9 << " GiB" << std::endl;
    // std::cout << "Availiable mem: " << monitor.getAvailableMem() * 1e-9 << " GiB" << std::endl;
    // std::cout << "Cached mem: " << monitor.getCachedMem() * 1e-9 << " GiB" << std::endl;
    // std::cout << "Active percent: " << monitor.getCpuActiveSec() / monitor.getCpuTotalSec() * 100.0 << std::endl;
    // std::vector<double> cpu_percentages = monitor.getCpuPerCoreActivePercent();
    // for (size_t i = 0; i < cpu_percentages.size(); i++) {
    //     std::cout << "Cpu" << i << " active %: " << cpu_percentages.at(i) * 100.0 << std::endl;
    // }

    ASSERT_GT(monitor.getTotalMem(), 0);
    ASSERT_GT(monitor.getAvailableMem(), 0);
    ASSERT_GT(monitor.getCachedMem(), 0);
    ASSERT_GT(monitor.getCpuActive(), 0);
    ASSERT_GT(monitor.getCpuTotal(), 0);
    ASSERT_GT(monitor.getCpuPerCoreActive().size(), 0);
}
