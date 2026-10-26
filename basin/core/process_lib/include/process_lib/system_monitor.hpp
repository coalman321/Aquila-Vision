#pragma once

#include <cstdint>
#include <deque>
#include <filesystem>
#include <string>
#include <vector>

namespace basin::process {

struct CpuStat {
    double user_time;
    double sys_time;
    double idle_time;
    double io_wait_time;

    // derived fields
    double non_idle_time{ 0 };
    double total_time{ 0 };
};

class SystemMonitor {
   public:
    SystemMonitor();

    void readStat();

    double getCpuTotal();

    double getCpuTotalAvgDelta();

    double getCpuActive();

    double getCpuActiveAverageDelta();

    std::vector<double> getCpuPerCoreActive() { return active_cpu_per_core_; }

    std::vector<double> getCpuPerCoreUser() { return user_cpu_per_core_; }

    std::vector<double> getCpuPerCoreSys() { return sys_cpu_per_core_; }

    uint64_t getTotalMem() { return total_sys_mem_; }

    uint64_t getAvailableMem() { return available_mem_; }

    uint64_t getUsedMem() { return total_sys_mem_ - available_mem_; }

    uint64_t getCachedMem() { return cached_mem_; }

   private:
    std::string readFullFile(std::filesystem::path file);

    std::string readProcField(const std::string& field_data, const std::string& field_name,
                              const std::string& delimiter);

    CpuStat readCpuStatInfo(const std::string& field_data);

    // CPU system information
    std::deque<double> cpu_total_time_hist_;
    std::deque<double> cpu_active_time_hist_;
    std::vector<double> active_cpu_per_core_;
    std::vector<double> user_cpu_per_core_;
    std::vector<double> sys_cpu_per_core_;
    size_t cpu_count_;

    // System memory information
    uint64_t total_sys_mem_;
    uint64_t available_mem_;
    uint64_t cached_mem_;
};
}  // namespace basin::process
