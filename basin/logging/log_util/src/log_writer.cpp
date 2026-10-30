#include "basin/log_util/log_writer.hpp"

#include <fcntl.h>
#include <rmw/rmw.h>
#include <unistd.h>

#include <chrono>
#include <filesystem>
#include <iomanip>
#include <rosbag2_storage/metadata_io.hpp>
#include <rosbag2_storage/serialized_bag_message.hpp>
#include <rosbag2_storage/topic_metadata.hpp>
#include <sstream>

namespace basin::log_util {

rosbag2_storage::StorageOptions make_mcap_storage_options(const std::string& directory, long split_time_s,
                                                          long split_size_bytes) {
    rosbag2_storage::StorageOptions options;
    options.uri = directory;
    options.max_bagfile_duration = split_time_s;
    options.max_bagfile_size = split_size_bytes;
    options.storage_id = "mcap";
    options.storage_config_uri = "";
    return options;
}

LogWriter::LogWriter() :
    is_open_(false),
    max_bagfile_duration_ns_(0),
    max_bagfile_size_(0),
    current_file_open_time_ns_(0),
    current_file_start_time_ns_(0),
    current_file_size_(0),
    current_split_count_(0) {}

LogWriter::~LogWriter() {
    if (is_open_) {
        close();
    }
}

void LogWriter::open(const rosbag2_storage::StorageOptions& storage_options,
                     const rosbag2_cpp::ConverterOptions& converter_options, const std::string& bag_name) {
    std::lock_guard<std::mutex> lock(mutex_);

    (void)converter_options;

    if (is_open_) {
        throw std::runtime_error("LogWriter is already open");
    }

    log_directory_ = storage_options.uri;
    base_filename_ = bag_name.empty() ? "log" : bag_name;
    max_bagfile_duration_ns_ = storage_options.max_bagfile_duration * 1000000000ULL;
    max_bagfile_size_ = storage_options.max_bagfile_size;
    current_split_count_ = 0;
    current_file_start_time_ns_ = 0;
    current_file_size_ = 0;

    if (!std::filesystem::exists(log_directory_)) {
        std::filesystem::create_directories(log_directory_);
    }

    std::string filename = generate_filename();
    std::filesystem::path filepath = std::filesystem::path(log_directory_) / filename;

    rosbag2_storage::StorageOptions file_options;
    file_options.uri = filepath.string();
    file_options.storage_id = storage_options.storage_id;

    storage_ = storage_factory_.open_read_write(file_options);

    if (!storage_) {
        throw std::runtime_error("Failed to open storage with ID: " + storage_options.storage_id);
    }

    // Determine actual on-disk path (MCAP plugin appends .mcap if not present)
    std::string candidate_with_ext = filepath.string() + ".mcap";
    current_file_path_ = std::filesystem::exists(candidate_with_ext) ? candidate_with_ext : filepath.string();

    metadata_.storage_identifier = storage_options.storage_id;
    metadata_.relative_file_paths.clear();
    metadata_.relative_file_paths.push_back(filename);
    metadata_.bag_size = 0;
    metadata_.version = 5;

    is_open_ = true;
    created_topics_.clear();
    current_file_open_time_ns_ =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();

    create_topic_internal("/log/metadata", "logging_msgs/msg/LogMetadata", "cdr");
}

void LogWriter::close() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!is_open_) {
        return;
    }

    if (storage_) {
        storage_.reset();
    }

    if (!log_directory_.empty()) {
        rosbag2_storage::MetadataIo metadata_io;
        metadata_io.write_metadata(log_directory_, metadata_);
    }

    is_open_ = false;
    created_topics_.clear();
}

bool LogWriter::is_open() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return is_open_;
}

void LogWriter::write(std::shared_ptr<const rclcpp::SerializedMessage> message, const std::string& topic_name,
                      const std::string& type_name, const rclcpp::Time& timestamp) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!is_open_ || !storage_) {
        throw std::runtime_error("LogWriter is not open");
    }

    if (created_topics_.find(topic_name) == created_topics_.end()) {
        rosbag2_storage::TopicMetadata topic_metadata;
        topic_metadata.name = topic_name;
        topic_metadata.type = type_name;
        topic_metadata.serialization_format = rmw_get_serialization_format();

        rosbag2_storage::MessageDefinition message_def;
        message_def.topic_type = type_name;
        storage_->create_topic(topic_metadata, message_def);
        metadata_.topics_with_message_count.push_back({ topic_metadata, 0 });
        created_topics_.insert(topic_name);
    }

    auto bag_message = std::make_shared<rosbag2_storage::SerializedBagMessage>();
    bag_message->topic_name = topic_name;
    bag_message->recv_timestamp = timestamp.nanoseconds();
    bag_message->serialized_data = std::shared_ptr<rcutils_uint8_array_t>(
        new rcutils_uint8_array_t(message->get_rcl_serialized_message()), [message](rcutils_uint8_array_t* data) {
            (void)message;
            if (data != nullptr) {
                data->buffer = nullptr;
                delete data;
            }
        });

    // Unlike the deprecated write(), write_message() reports failure by returning false. The MCAP
    // plugin's write() threw here, so keep that (and don't count a message that never landed).
    if (!storage_->write_message(bag_message)) {
        throw std::runtime_error("Message on topic '" + topic_name + "' failed to write to storage");
    }

    current_file_size_ += bag_message->serialized_data->buffer_length;
    metadata_.bag_size += bag_message->serialized_data->buffer_length;

    for (auto& topic : metadata_.topics_with_message_count) {
        if (topic.topic_metadata.name == topic_name) {
            topic.message_count++;
            break;
        }
    }

    if (current_file_start_time_ns_ == 0) {
        current_file_start_time_ns_ = timestamp.nanoseconds();
    }

    split_if_needed();
}

void LogWriter::write_metadata_event(const logging_msgs::msg::LogMetadata& metadata, const rclcpp::Time& timestamp) {
    auto serialized_msg = std::make_shared<rclcpp::SerializedMessage>();
    rclcpp::Serialization<logging_msgs::msg::LogMetadata> serialization;
    serialization.serialize_message(&metadata, serialized_msg.get());
    write(serialized_msg, "/log/metadata", "logging_msgs/msg/LogMetadata", timestamp);
}

void LogWriter::create_topic(const std::string& topic_name, const std::string& type_name,
                             const std::string& serialization_format) {
    std::lock_guard<std::mutex> lock(mutex_);
    create_topic_internal(topic_name, type_name, serialization_format);
}

void LogWriter::create_topic_internal(const std::string& topic_name, const std::string& type_name,
                                      const std::string& serialization_format) {
    if (!is_open_ || !storage_) {
        throw std::runtime_error("LogWriter is not open");
    }

    if (created_topics_.find(topic_name) != created_topics_.end()) {
        return;
    }

    rosbag2_storage::TopicMetadata topic_metadata;
    topic_metadata.name = topic_name;
    topic_metadata.type = type_name;
    topic_metadata.serialization_format = serialization_format;

    rosbag2_storage::MessageDefinition message_def;
    message_def.topic_type = type_name;

    storage_->create_topic(topic_metadata, message_def);

    metadata_.topics_with_message_count.push_back({ topic_metadata, 0 });
    created_topics_.insert(topic_name);
}

std::string LogWriter::get_recording_directory() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return log_directory_;
}

std::string LogWriter::get_current_file_path() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_file_path_;
}

void LogWriter::force_split() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!is_open_ || !storage_) {
        return;
    }
    do_split_unlocked();
}

void LogWriter::check_split() {
    std::lock_guard<std::mutex> lock(mutex_);
    split_if_needed();
}

std::string LogWriter::generate_filename() {
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(6) << current_split_count_;
    return base_filename_ + "_" + oss.str();
}

void LogWriter::split_if_needed() {
    if (!is_open_ || !storage_) {
        return;
    }

    bool should_split = false;

    if (max_bagfile_duration_ns_ > 0 && current_file_open_time_ns_ > 0) {
        uint64_t current_time_ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
        uint64_t elapsed_ns = current_time_ns - current_file_open_time_ns_;
        if (elapsed_ns >= max_bagfile_duration_ns_) {
            should_split = true;
        }
    }

    if (max_bagfile_size_ > 0 && current_file_size_ >= max_bagfile_size_) {
        should_split = true;
    }

    if (!should_split) {
        return;
    }

    do_split_unlocked();
}

void LogWriter::do_split_unlocked() {
    // Sync the current file to disk before closing it (Phase 3)
    if (!current_file_path_.empty()) {
        int fd = ::open(current_file_path_.c_str(), O_RDONLY);
        if (fd >= 0) {
            ::fdatasync(fd);
            ::close(fd);
        }
    }

    if (storage_) {
        storage_.reset();
    }

    current_split_count_++;
    current_file_open_time_ns_ =
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
    current_file_start_time_ns_ = 0;
    current_file_size_ = 0;

    std::string filename = generate_filename();
    std::filesystem::path filepath = std::filesystem::path(log_directory_) / filename;

    rosbag2_storage::StorageOptions file_options;
    file_options.uri = filepath.string();
    file_options.storage_id = metadata_.storage_identifier;

    storage_ = storage_factory_.open_read_write(file_options);

    if (!storage_) {
        throw std::runtime_error("Failed to open new storage file for split");
    }

    // Update tracked path for the new file
    std::string candidate_with_ext = filepath.string() + ".mcap";
    current_file_path_ = std::filesystem::exists(candidate_with_ext) ? candidate_with_ext : filepath.string();

    metadata_.relative_file_paths.push_back(filename);

    std::set<std::string> topics_to_recreate = created_topics_;
    created_topics_.clear();

    for (const auto& topic_name : topics_to_recreate) {
        for (const auto& topic_with_count : metadata_.topics_with_message_count) {
            if (topic_with_count.topic_metadata.name == topic_name) {
                rosbag2_storage::MessageDefinition message_def;
                message_def.topic_type = topic_with_count.topic_metadata.type;
                storage_->create_topic(topic_with_count.topic_metadata, message_def);
                created_topics_.insert(topic_name);
                break;
            }
        }
    }
}

}  // namespace basin::log_util
