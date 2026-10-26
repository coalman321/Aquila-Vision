#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "basin/log_util/snapshot_writer.hpp"

namespace basin::log_util {

/// @brief CLI tool for extracting snapshots from MCAP log files
///
/// This tool takes a set of MCAP log files and a snapshots.yaml file,
/// and extracts the data for specific snapshots into separate rosbag directories.
class SnapshotExtractor {
   public:
    SnapshotExtractor();
    ~SnapshotExtractor();

    /// @brief Extracts a single snapshot to a new rosbag directory
    /// @param log_directory Directory containing MCAP files and snapshots.yaml
    /// @param snapshot_id ID of the snapshot to extract
    /// @param output_directory Directory where the extracted bag will be created
    /// @return true if extraction was successful
    bool extract_snapshot(const std::string& log_directory, const std::string& snapshot_id,
                          const std::string& output_directory);

    /// @brief Extracts all snapshots from a log directory
    /// @param log_directory Directory containing MCAP files and snapshots.yaml
    /// @param output_base_directory Base directory where snapshot bags will be created
    /// @return Number of snapshots successfully extracted
    int extract_all_snapshots(const std::string& log_directory, const std::string& output_base_directory);

    /// @brief Lists all available snapshots in a log directory
    /// @param log_directory Directory containing snapshots.yaml
    /// @return Vector of snapshot information
    std::vector<SnapshotInfo> list_snapshots(const std::string& log_directory);

    /// @brief Validates that a log directory has the required files
    /// @param log_directory Directory to validate
    /// @param error_message Output parameter for error details
    /// @return true if directory is valid
    bool validate_log_directory(const std::string& log_directory, std::string& error_message);

    /// @brief Finds all MCAP files in a directory that overlap with a time range
    /// @param log_directory Directory containing MCAP files
    /// @param start_time Start time in epoch seconds
    /// @param end_time End time in epoch seconds
    /// @return Vector of MCAP file paths
    std::vector<std::filesystem::path> find_relevant_mcap_files(const std::string& log_directory, int64_t start_time,
                                                                int64_t end_time);

   private:
    /// @brief Extracts data from MCAP files for a given time range
    /// @param mcap_files List of MCAP files to read
    /// @param start_time Start time in epoch seconds
    /// @param end_time End time in epoch seconds
    /// @param output_bag_path Path to output rosbag directory
    /// @return true if extraction was successful
    bool extract_time_range(const std::vector<std::filesystem::path>& mcap_files, int64_t start_time, int64_t end_time,
                            const std::string& output_bag_path);

    /// @brief Creates the output directory structure for a snapshot
    /// @param output_directory Base output directory
    /// @param snapshot_id Snapshot ID (used for directory name)
    /// @return Path to the created snapshot directory
    std::filesystem::path create_output_directory(const std::string& output_directory, const std::string& snapshot_id);

    /// @brief Writes snapshot metadata to the output directory
    /// @param output_directory Output snapshot directory
    /// @param snapshot Snapshot information to write
    /// @return true if metadata was written successfully
    bool write_snapshot_metadata(const std::filesystem::path& output_directory, const SnapshotInfo& snapshot);

    /// @brief Gets the time range of an MCAP file
    /// @param mcap_file Path to MCAP file
    /// @param start_time Output parameter for start time
    /// @param end_time Output parameter for end time
    /// @return true if time range was successfully read
    bool get_mcap_time_range(const std::filesystem::path& mcap_file, int64_t& start_time, int64_t& end_time);
};

}  // namespace basin::log_util
