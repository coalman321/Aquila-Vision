#pragma once

#include <pybind11/embed.h>

#include <filesystem>
#include <optional>
#include <process_lib/process_controller.hpp>
#include <string>
#include <vector>
#include <rclcpp/logger.hpp>

namespace basin::launch_core {
struct ProcessDescription {
    ///< Fully qualified path to the executable
    std::filesystem::path exe_path;

    ///< List of arguments to pass into the executable
    std::vector<std::string> args;

    ///< Unique name used to identify the process
    std::string process_name;

    ///< unique identifier for this process
    uint32_t id;

    ///< Scheduler settings for the process, unset inherits them from the launch node
    std::optional<basin::process::SchedulingConfig> scheduling;
};

class LaunchInterpreter {
   public:
    LaunchInterpreter();

    ~LaunchInterpreter();

    /// @brief Loads a python production launch file from a full path
    /// @param launch_path The full path to the launch file
    /// @param id_offset Offset for process IDs (default: 0)
    void loadDescription(std::filesystem::path launch_path, int id_offset=0);

    /// @brief Loads a python production launch file from a package and launch file name
    /// @param package The name of the package to search within
    /// @param launch_file The name of the launch file in the package
    /// @param id_offset Offset for process IDs (default: 0)
    void loadDescription(const std::string& package, const std::string& launch_file, int id_offset=0);

    /// @brief Gets the list of process descriptions the launch file has requested to load
    /// @return A vector of processes to spawn
    std::vector<ProcessDescription> getProcessDescription();

    /// @brief Gets the fully resolved launch path that was loaded
    /// @return The path to the resolved launch file
    std::filesystem::path getLaunchPath();

   private:
    /// @brief Builds the launch description after the launch_path_ has been loaded by the constructor
    /// @details The constructor should call this after the launch_path_ is set.
    /// @param id_offset Offset for process IDs
    void buildDescription(int id_offset);

    std::filesystem::path launch_path_;  ///< The path to the launch file

    std::vector<ProcessDescription> launch_processes_;  ///< The processes declared by the launch file

    rclcpp::Logger logger_ = rclcpp::get_logger("LaunchInterpreter");
};
}  // namespace basin::launch_core
