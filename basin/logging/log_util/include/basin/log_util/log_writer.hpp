#pragma once

#include <logging_msgs/msg/log_metadata.hpp>
#include <memory>
#include <mutex>
#include <rclcpp/rclcpp.hpp>
#include <map>
#include <rosbag2_cpp/converter_options.hpp>
#include <rosbag2_cpp/message_definitions/local_message_definition_source.hpp>
#include <rosbag2_storage/message_definition.hpp>
#include <rosbag2_storage/storage_factory.hpp>
#include <rosbag2_storage/storage_interfaces/read_write_interface.hpp>
#include <rosbag2_storage/storage_options.hpp>
#include <set>
#include <string>

namespace basin::log_util {

/// @brief Storage options for an MCAP log written into `directory`
/// @param directory The log directory; each split is a flat `<bag_name>_<split>.mcap` file inside it
/// @param split_time_s Seconds of recording per split file
/// @param split_size_bytes Largest size of a split file in bytes
rosbag2_storage::StorageOptions make_mcap_storage_options(const std::string& directory, long split_time_s,
                                                          long split_size_bytes);

/// @brief Handles writing MCAP files to disk with time/size-based splitting
///
/// This component manages the main log writer that handles writing MCAP files to disk.
/// It handles the following tasks:
/// - Splitting files based on time or size
/// - Writing metadata events
/// - Managing topic creation
class LogWriter {
   public:
    LogWriter();
    ~LogWriter();

    /// @brief Opens a new log file for writing
    /// @param storage_options Storage configuration including path, split time/size
    /// @param converter_options Serialization format options
    /// @param bag_name Optional base name for the bag (if empty, uses last component of URI)
    void open(const rosbag2_storage::StorageOptions& storage_options,
              const rosbag2_cpp::ConverterOptions& converter_options = rosbag2_cpp::ConverterOptions(),
              const std::string& bag_name = "");

    /// @brief Closes the current log file
    void close();

    /// @brief Checks if the writer is currently open
    /// @return true if open, false otherwise
    bool is_open() const;

    /// @brief Writes a serialized message to the log
    /// @param message The serialized message to write
    /// @param topic_name The name of the topic
    /// @param type_name The message type name
    /// @param timestamp The timestamp of the message
    void write(std::shared_ptr<const rclcpp::SerializedMessage> message, const std::string& topic_name,
               const std::string& type_name, const rclcpp::Time& timestamp);

    /// @brief Writes a metadata event to the log as a special channel
    /// @param metadata The metadata message to write
    /// @param timestamp The timestamp of the message
    void write_metadata_event(const logging_msgs::msg::LogMetadata& metadata, const rclcpp::Time& timestamp);

    /// @brief Creates a topic in the bag file if it doesn't already exist
    /// @param topic_name The name of the topic
    /// @param type_name The message type name
    /// @param serialization_format The serialization format (default: "cdr")
    void create_topic(const std::string& topic_name, const std::string& type_name,
                      const std::string& serialization_format = "cdr");

    /// @brief Gets the current recording directory path
    /// @return The path to the current recording directory
    std::string get_recording_directory() const;

    /// @brief Gets the on-disk path of the MCAP file currently being written
    /// @return The path, or an empty string if the writer has never been opened
    std::string get_current_file_path() const;

    /// @brief Forces an immediate split to a new MCAP file, closing and syncing the current one.
    /// Call this before emitting a snapshot manifest so all data is in a readable closed file.
    void force_split();

    /// @brief Checks whether a time/size-based split is due and performs it if so.
    /// Safe to call from outside the write path (e.g. from a timer).
    void check_split();

   private:
    void split_if_needed();
    void do_split_unlocked();  // performs the actual split; must be called with mutex_ held
    std::string generate_filename();

    // Internal version without mutex lock (for use when mutex already held)
    void create_topic_internal(const std::string& topic_name, const std::string& type_name,
                               const std::string& serialization_format);

    // Full `.msg` text for a type (encoding "ros2msg"), so MCAP schemas are self-describing for
    // readers like Foxglove. Falls back to an empty definition if the type isn't in the ament index.
    rosbag2_storage::MessageDefinition lookup_message_definition(const std::string& topic_name,
                                                                 const std::string& type_name);

    mutable std::mutex mutex_;
    std::shared_ptr<rosbag2_storage::storage_interfaces::ReadWriteInterface> storage_;
    rosbag2_storage::StorageFactory storage_factory_;
    std::string log_directory_;
    std::string base_filename_;
    std::string current_file_path_;  // actual on-disk path of the currently-open MCAP file
    bool is_open_;
    std::set<std::string> created_topics_;
    rosbag2_cpp::LocalMessageDefinitionSource message_definitions_;
    std::map<std::string, rosbag2_storage::MessageDefinition> topic_definitions_;  // topic -> definition

    // Split management
    uint64_t max_bagfile_duration_ns_;
    uint64_t max_bagfile_size_;
    uint64_t current_file_open_time_ns_;   // wall-clock ns when the current file was opened
    uint64_t current_file_start_time_ns_;  // timestamp of first message written to current file
    uint64_t current_file_size_;
    int current_split_count_;

    rosbag2_storage::BagMetadata metadata_;
};

}  // namespace basin::log_util
