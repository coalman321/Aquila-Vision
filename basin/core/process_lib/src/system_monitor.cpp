
#include "process_lib/system_monitor.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

namespace basin::process {

SystemMonitor::SystemMonitor() {
    // read cpu stats to get num cores on startup
    cpu_count_ = std::thread::hardware_concurrency();
}

double SystemMonitor::getCpuTotal() {
    if (cpu_total_time_hist_.empty()) {
        return 0.0;
    }

    return *cpu_total_time_hist_.begin();
}

double SystemMonitor::getCpuTotalAvgDelta() {
    double sum_cpu_delta{ 0.0 };
    for (size_t i = 0; i < cpu_total_time_hist_.size() - 1; i++) {
        sum_cpu_delta += cpu_total_time_hist_.at(i) - cpu_total_time_hist_.at(i + 1);
    }

    return sum_cpu_delta / static_cast<double>(cpu_total_time_hist_.size());
}

double SystemMonitor::getCpuActive() {
    if (cpu_active_time_hist_.empty()) {
        return 0.0;
    }

    return *cpu_active_time_hist_.begin();
}

double SystemMonitor::getCpuActiveAverageDelta() {
    double sum_cpu_delta{ 0.0 };
    for (size_t i = 0; i < cpu_active_time_hist_.size() - 1; i++) {
        sum_cpu_delta += cpu_active_time_hist_.at(i) - cpu_active_time_hist_.at(i + 1);
    }

    return sum_cpu_delta / static_cast<double>(cpu_active_time_hist_.size());
}

void SystemMonitor::readStat() {
    // build the path to the stat file and read it
    fs::path stat_file{ "/proc/stat" };
    std::string stat_data = readFullFile(stat_file);

    // read the main CPU field
    std::string cpu_field = readProcField(stat_data, "cpu", "\n");
    CpuStat main_stats = readCpuStatInfo(cpu_field);
    cpu_total_time_hist_.push_back(main_stats.total_time);
    cpu_active_time_hist_.push_back(main_stats.non_idle_time);

    // ensure histories are pruned as needed
    if (cpu_total_time_hist_.size() > 10) {
        cpu_total_time_hist_.pop_front();
    }
    if (cpu_active_time_hist_.size() > 10) {
        cpu_active_time_hist_.pop_front();
    }

    // read stats to get per-cpu idle
    active_cpu_per_core_.clear();
    user_cpu_per_core_.clear();
    sys_cpu_per_core_.clear();
    for (size_t i = 0; i < cpu_count_; i++) {
        std::string cpu_idx_field = readProcField(stat_data, "cpu" + std::to_string(i), "\n");
        CpuStat per_core_stats = readCpuStatInfo(cpu_idx_field);

        active_cpu_per_core_.push_back(per_core_stats.non_idle_time / per_core_stats.total_time);
        user_cpu_per_core_.push_back(per_core_stats.user_time / per_core_stats.total_time);
        sys_cpu_per_core_.push_back(per_core_stats.sys_time / per_core_stats.total_time);
    }

    // build the path to the meminfo file and read it
    fs::path mem_file{ "/proc/meminfo" };
    std::string mem_data = readFullFile(mem_file);

    // find each of the fields we need
    total_sys_mem_ = std::stoull(readProcField(mem_data, "MemTotal:", "kB")) * 1000;
    available_mem_ = std::stoull(readProcField(mem_data, "MemAvailable:", "kB")) * 1000;
    cached_mem_ = std::stoull(readProcField(mem_data, "Cached:", "kB")) * 1000;
}

std::string SystemMonitor::readFullFile(std::filesystem::path file) {
    // make sure stat exists. if it doesnt, set pid to invalid
    if (!fs::is_regular_file(file)) {
        throw std::runtime_error(file.string() + " is not a regular file...");
    }

    // open the stat file for reading
    std::string file_data;
    file_data.resize(4096);
    int file_fd = open(file.c_str(), O_RDONLY);
    int true_fsize = read(file_fd, file_data.data(), file_data.size());
    close(file_fd);
    file_data.resize(true_fsize);

    // verify data was read
    if (true_fsize == 0) {
        throw std::runtime_error("Failed to read " + file.string());
    }

    return file_data;
}

std::string SystemMonitor::readProcField(const std::string& field_data, const std::string& field_name,
                                         const std::string& delimiter) {
    size_t field_name_index = field_data.find(field_name);
    if (field_name_index == std::string::npos) {
        throw std::runtime_error("Unable to find meminfo field " + field_name);
    }

    // find the following newline
    std::string field_begin = field_data.substr(field_name_index + field_name.size());
    size_t byte_unit_index = field_begin.find(delimiter);

    // get the string with just the data
    std::string str_data = field_begin.substr(0, byte_unit_index);
    return str_data;
}

CpuStat SystemMonitor::readCpuStatInfo(const std::string& field_data) {
    CpuStat stat;
    std::string cpu_data = field_data;

    // remove leading whitespace
    while (cpu_data.at(1) == ' ') {
        cpu_data = cpu_data.substr(1);
    }

    // parse out the values
    size_t pos = 0;
    int split_index = 0;
    while (pos != std::string::npos) {
        // slice the remainder of process data down
        cpu_data = cpu_data.substr(pos + 1);
        pos = cpu_data.find(" ");

        // get the next value as a string
        std::string val_str = cpu_data.substr(0, pos);
        double field_value = std::stod(val_str) / sysconf(_SC_CLK_TCK);

        // get the total time
        stat.total_time += field_value;

        // parse optional values
        if (split_index == 0) {
            stat.user_time = field_value;
            stat.non_idle_time += field_value;
        } else if (split_index == 2) {
            stat.sys_time = field_value;
            stat.non_idle_time += field_value;
        } else if (split_index == 3) {
            stat.idle_time = field_value;
        } else if (split_index == 4) {
            stat.io_wait_time = field_value;
            stat.non_idle_time += field_value;
        } else {
            stat.non_idle_time += field_value;
        }

        split_index++;
    }

    return stat;
}

}  // namespace basin::process
