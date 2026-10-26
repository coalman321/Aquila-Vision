
#include "process_lib/process_monitor.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

namespace basin::process {

void ProcessMonitor::setPID(pid_t pid) { monitored_pid_ = pid; }

void ProcessMonitor::readStat() {
    // build the path to the stat file
    fs::path stat_file{ "/proc" };
    stat_file /= std::to_string(monitored_pid_);
    stat_file /= "stat";

    // open the stat file for reading. if it doesnt exist, set pid to invalid
    int stat_fd = open(stat_file.c_str(), O_RDONLY);
    if (stat_fd < 0) {
        monitored_pid_ = INACTIVE_PID;
        process_name_ = "";
        return;
    }

    std::string stat_line;
    stat_line.resize(4096);
    ssize_t true_fsize = read(stat_fd, stat_line.data(), stat_line.size());
    close(stat_fd);

    // the process can exit between the open and the read, in which case there is nothing left to read
    if (true_fsize <= 0) {
        monitored_pid_ = INACTIVE_PID;
        process_name_ = "";
        return;
    }
    stat_line.resize(true_fsize);

    // parse out the process name
    size_t name_begin = stat_line.find('(') + 1;
    size_t name_end = stat_line.find(')');
    process_name_ = stat_line.substr(name_begin, name_end - name_begin);

    // parse out the remaining values
    std::string process_data = stat_line.substr(name_end + 1);
    size_t pos = 0;
    int split_index = 0;
    while (pos != std::string::npos) {
        // slice the remainder of process data down
        process_data = process_data.substr(pos + 1);
        pos = process_data.find(" ");

        // get the next value as a string
        std::string val_str = process_data.substr(0, pos);

        if (split_index == 0) {
            // status code string
        } else if (split_index == 11) {
            user_time_ = std::stod(val_str) / sysconf(_SC_CLK_TCK);
        } else if (split_index == 12) {
            sys_time_ = std::stod(val_str) / sysconf(_SC_CLK_TCK);
        } else if (split_index == 21) {
            vm_size_bytes_ = std::stoi(val_str) * 1000;
        } else if (split_index > 21) {
            break;
        } else {
            // wait to get to another relevant value
        }

        split_index++;
    }
}
}  // namespace basin::process
