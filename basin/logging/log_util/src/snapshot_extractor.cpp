#include "basin/log_util/snapshot_extractor.hpp"

#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <rclcpp/serialization.hpp>
#include <rclcpp/serialized_message.hpp>
#include <rosbag2_cpp/reader.hpp>
#include <rosbag2_cpp/readers/sequential_reader.hpp>
#include <rosbag2_cpp/writer.hpp>
#include <rosbag2_cpp/writers/sequential_writer.hpp>
#include <rosbag2_storage/storage_factory.hpp>
#include <rosbag2_storage/storage_options.hpp>

#include "basin/log_util/snapshot_writer.hpp"

namespace basin::log_util {

SnapshotExtractor::SnapshotExtractor() {}

SnapshotExtractor::~SnapshotExtractor() {}

bool SnapshotExtractor::validate_log_directory(const std::string& log_directory, std::string& error_message) {
    std::filesystem::path dir_path(log_directory);

    // Check if directory exists
    if (!std::filesystem::exists(dir_path)) {
        error_message = "Directory does not exist: " + log_directory;
        return false;
    }

    if (!std::filesystem::is_directory(dir_path)) {
        error_message = "Path is not a directory: " + log_directory;
        return false;
    }

    // Check for snapshots.yaml
    std::filesystem::path snapshots_file = dir_path / "snapshots.yaml";
    if (!std::filesystem::exists(snapshots_file)) {
        error_message = "snapshots.yaml not found in directory: " + log_directory;
        return false;
    }

    // Check for at least one MCAP file
    bool has_mcap = false;
    for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
        if (entry.path().extension() == ".mcap") {
            has_mcap = true;
            break;
        }
    }

    if (!has_mcap) {
        error_message = "No MCAP files found in directory: " + log_directory;
        return false;
    }

    return true;
}

std::vector<SnapshotInfo> SnapshotExtractor::list_snapshots(const std::string& log_directory) {
    SnapshotWriter writer;
    writer.set_recording_directory(log_directory);

    if (!writer.load_from_file()) {
        std::cerr << "Failed to load snapshots from: " << log_directory << std::endl;
        return {};
    }

    return writer.get_all_snapshots();
}

bool SnapshotExtractor::get_mcap_time_range(const std::filesystem::path& mcap_file, int64_t& start_time,
                                            int64_t& end_time) {
    try {
        rosbag2_storage::StorageFactory factory;
        rosbag2_storage::StorageOptions options;
        options.uri = mcap_file.string();
        options.storage_id = "mcap";

        auto storage = factory.open_read_only(options);
        if (!storage) {
            std::cerr << "Failed to open MCAP file: " << mcap_file << std::endl;
            return false;
        }

        auto metadata = storage->get_metadata();

        // Convert ROS time (nanoseconds) to epoch seconds
        start_time = metadata.starting_time.time_since_epoch().count() / 1000000000LL;

        // Calculate end time from start time and duration
        int64_t duration_ns = metadata.duration.count();
        end_time = start_time + (duration_ns / 1000000000LL);

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Exception reading MCAP file: " << mcap_file << " - " << e.what() << std::endl;
        return false;
    }
}

std::vector<std::filesystem::path> SnapshotExtractor::find_relevant_mcap_files(const std::string& log_directory,
                                                                               int64_t start_time, int64_t end_time) {
    std::vector<std::filesystem::path> relevant_files;
    std::filesystem::path dir_path(log_directory);

    for (const auto& entry : std::filesystem::directory_iterator(dir_path)) {
        if (entry.path().extension() != ".mcap") {
            continue;
        }

        // Skip zero-byte files — they are the currently-open (un-finalised) active split.
        if (std::filesystem::file_size(entry.path()) == 0) {
            continue;
        }

        int64_t file_start, file_end;
        if (!get_mcap_time_range(entry.path(), file_start, file_end)) {
            // The file exists and is non-empty but the MCAP footer is absent or corrupt
            // (e.g. a file that was just finalised and the footer wasn't flushed yet, or
            // a quiet split whose MCAP metadata reports an empty time range).
            // Fall back to filesystem modification time so we don't silently exclude it.
            auto mtime = std::filesystem::last_write_time(entry.path());
            auto mtime_sys = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                mtime - std::filesystem::file_time_type::clock::now() + std::chrono::system_clock::now());
            file_end = std::chrono::duration_cast<std::chrono::seconds>(mtime_sys.time_since_epoch()).count();
            // Assume the file covers up to log_split_time seconds before its mtime.
            // Using a generous 300-second window so we never miss data.
            file_start = file_end - 300;
        }

        // Check if time ranges overlap
        // Files overlap if: file_start <= end_time && file_end >= start_time
        if (file_start <= end_time && file_end >= start_time) {
            relevant_files.push_back(entry.path());
        }
    }

    // Sort files by name (typically chronological)
    std::sort(relevant_files.begin(), relevant_files.end());

    return relevant_files;
}

std::filesystem::path SnapshotExtractor::create_output_directory(const std::string& output_directory,
                                                                 const std::string& snapshot_id) {
    std::filesystem::path base_path(output_directory);
    std::filesystem::path snapshot_path = base_path / snapshot_id;

    // Create only the parent directory, let rosbag2 create the actual bag directory
    std::filesystem::create_directories(base_path);

    return snapshot_path;
}

bool SnapshotExtractor::write_snapshot_metadata(const std::filesystem::path& output_directory,
                                                const SnapshotInfo& snapshot) {
    try {
        YAML::Node root;
        root["version"] = 1;

        YAML::Node snapshot_node;
        snapshot_node["id"] = snapshot.id;
        snapshot_node["start_time"] = snapshot.start_time;
        snapshot_node["end_time"] = snapshot.end_time;
        snapshot_node["trigger"] = snapshot.trigger;
        snapshot_node["description"] = snapshot.description;
        snapshot_node["tags"] = snapshot.tags;

        root["snapshots"].push_back(snapshot_node);

        std::filesystem::path yaml_path = output_directory / "snapshots.yaml";
        std::ofstream fout(yaml_path);
        fout << root;
        fout.close();

        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to write snapshot metadata: " << e.what() << std::endl;
        return false;
    }
}

bool SnapshotExtractor::extract_time_range(const std::vector<std::filesystem::path>& mcap_files, int64_t start_time,
                                           int64_t end_time, const std::string& output_bag_path) {
    try {
        // Setup rosbag2 writer
        rosbag2_cpp::Writer writer;

        rosbag2_storage::StorageOptions storage_options;
        storage_options.uri = output_bag_path;
        storage_options.storage_id = "mcap";

        rosbag2_cpp::ConverterOptions converter_options;
        converter_options.input_serialization_format = "cdr";
        converter_options.output_serialization_format = "cdr";

        writer.open(storage_options, converter_options);

        // Track created topics to avoid duplicates
        std::map<std::string, bool> created_topics;

        // Convert times to nanoseconds for comparison
        int64_t start_ns = start_time * 1000000000LL;
        int64_t end_ns = end_time * 1000000000LL;

        // Process each MCAP file
        for (const auto& mcap_file : mcap_files) {
            std::cout << "Processing: " << mcap_file.filename() << std::endl;

            // Setup rosbag2 reader
            rosbag2_cpp::Reader reader;
            rosbag2_storage::StorageOptions read_options;
            read_options.uri = mcap_file.string();
            read_options.storage_id = "mcap";

            rosbag2_cpp::ConverterOptions read_converter_options;
            read_converter_options.input_serialization_format = "cdr";
            read_converter_options.output_serialization_format = "cdr";

            try {
                reader.open(read_options, read_converter_options);
            } catch (const std::exception& e) {
                std::cerr << "Failed to open MCAP file: " << mcap_file << " - " << e.what() << std::endl;
                continue;
            }

            // Read messages
            while (reader.has_next()) {
                auto bag_message = reader.read_next();

                // Check if message is within time range
                if (static_cast<int64_t>(bag_message->recv_timestamp) < start_ns ||
                    static_cast<int64_t>(bag_message->recv_timestamp) > end_ns) {
                    continue;
                }

                // Create topic if not already created
                std::string topic_name = bag_message->topic_name;
                if (created_topics.find(topic_name) == created_topics.end()) {
                    // Get topic metadata from reader
                    auto topics_and_types = reader.get_all_topics_and_types();
                    for (const auto& topic_metadata : topics_and_types) {
                        if (topic_metadata.name == topic_name) {
                            writer.create_topic(topic_metadata);
                            created_topics[topic_name] = true;
                            break;
                        }
                    }
                }

                // Write message
                writer.write(bag_message);
            }

            reader.close();
        }

        writer.close();
        return true;

    } catch (const std::exception& e) {
        std::cerr << "Error during extraction: " << e.what() << std::endl;
        return false;
    }
}

bool SnapshotExtractor::extract_snapshot(const std::string& log_directory, const std::string& snapshot_id,
                                         const std::string& output_directory) {
    // Validate log directory
    std::string error_msg;
    if (!validate_log_directory(log_directory, error_msg)) {
        std::cerr << "Invalid log directory: " << error_msg << std::endl;
        return false;
    }

    // Load snapshots
    SnapshotWriter writer;
    writer.set_recording_directory(log_directory);
    if (!writer.load_from_file()) {
        std::cerr << "Failed to load snapshots from: " << log_directory << std::endl;
        return false;
    }

    // Find the requested snapshot
    if (!writer.has_snapshot(snapshot_id)) {
        std::cerr << "Snapshot not found: " << snapshot_id << std::endl;
        return false;
    }

    SnapshotInfo snapshot = writer.get_snapshot(snapshot_id);

    std::cout << "Extracting snapshot: " << snapshot.id << std::endl;
    std::cout << "  Time range: " << snapshot.start_time << " - " << snapshot.end_time << std::endl;
    std::cout << "  Description: " << snapshot.description << std::endl;

    // Find relevant MCAP files
    auto mcap_files = find_relevant_mcap_files(log_directory, snapshot.start_time, snapshot.end_time);

    if (mcap_files.empty()) {
        std::cerr << "No MCAP files found for snapshot time range" << std::endl;
        return false;
    }

    std::cout << "Found " << mcap_files.size() << " relevant MCAP files" << std::endl;

    // Create output directory
    auto output_path = create_output_directory(output_directory, snapshot.id);
    std::cout << "Output directory: " << output_path << std::endl;

    // Extract data to rosbag
    if (!extract_time_range(mcap_files, snapshot.start_time, snapshot.end_time, output_path.string())) {
        std::cerr << "Failed to extract snapshot data" << std::endl;
        return false;
    }

    // Write snapshot metadata
    if (!write_snapshot_metadata(output_path, snapshot)) {
        std::cerr << "Failed to write snapshot metadata" << std::endl;
        return false;
    }

    std::cout << "Successfully extracted snapshot: " << snapshot.id << std::endl;
    return true;
}

int SnapshotExtractor::extract_all_snapshots(const std::string& log_directory,
                                             const std::string& output_base_directory) {
    // Validate log directory
    std::string error_msg;
    if (!validate_log_directory(log_directory, error_msg)) {
        std::cerr << "Invalid log directory: " << error_msg << std::endl;
        return 0;
    }

    // Load all snapshots
    auto snapshots = list_snapshots(log_directory);
    if (snapshots.empty()) {
        std::cerr << "No snapshots found in: " << log_directory << std::endl;
        return 0;
    }

    std::cout << "Found " << snapshots.size() << " snapshots to extract" << std::endl;

    int success_count = 0;
    for (const auto& snapshot : snapshots) {
        std::cout << "\n--- Extracting snapshot " << (success_count + 1) << "/" << snapshots.size() << " ---"
                  << std::endl;

        if (extract_snapshot(log_directory, snapshot.id, output_base_directory)) {
            success_count++;
        } else {
            std::cerr << "Failed to extract snapshot: " << snapshot.id << std::endl;
        }
    }

    std::cout << "\n=== Extraction complete ===" << std::endl;
    std::cout << "Successfully extracted " << success_count << " out of " << snapshots.size() << " snapshots"
              << std::endl;

    return success_count;
}

}  // namespace basin::log_util
