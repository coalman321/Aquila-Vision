#include "continous_logger/logger_node.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <system_error>

#include "continous_logger/logger_status.hpp"

using namespace std::chrono_literals;
using namespace std::placeholders;

namespace basin::continuous_logger {

// The fault raised while the log disk is at or above its warning threshold. It is reported once per disk
// check, so it stays live for this many check intervals: long enough that a late check does not let it lapse,
// short enough that it clears soon after a check finds the disk back under the threshold.
static constexpr const char* DISK_FAULT_SYSTEM = "continuous_logger";
static constexpr const char* DISK_FAULT_NAME = "disk_usage_high";
static constexpr double DISK_FAULT_TOV_INTERVALS = 2.0;

// Compiles a topic pattern to a regex: `*` matches one name segment and everything else matches literally.
static std::regex compileTopicPattern(const std::string& pattern) {
    std::string expr;
    for (char c : pattern) {
        if (c == '*') {
            expr += "[^/]+";
        } else {
            if (std::strchr(".^$|()[]{}+?\\", c) != nullptr) {
                expr += '\\';
            }
            expr += c;
        }
    }
    return std::regex(expr);
}

LoggerNode::LoggerNode(const rclcpp::NodeOptions& options) : basin::node_core::NodeCore("logger", "", options) {
    params_ = std::make_unique<LoggerParams>(this);

    // Create the component instances
    log_writer_ = std::make_unique<log_util::LogWriter>();
    snapshot_tracker_ = std::make_unique<log_util::SnapshotTracker>();
    log_manager_ = std::make_unique<log_util::LogManager>();

    // Configure log manager
    log_manager_->set_log_directory(params_->log_path.string());
    log_manager_->set_disk_limits(params_->rotation_threshold_percent, params_->snapshot_usage_threshold_percent);

    // Resolve manifest directory (default: {log_path}/pending)
    manifest_dir_ = params_->manifest_dir.empty() ? params_->log_path / "pending" : params_->manifest_dir;
    log_manager_->set_manifest_dir(manifest_dir_.string());

    // Create the log directory, then scan the logs already in it
    try {
        log_manager_->prepare_log_directory();
    } catch (const std::exception& e) {
        RCLCPP_FATAL_STREAM(get_logger(), "Configured log path is not a valid directory! " << params_->log_path);
        throw;
    }
    log_manager_->scan_log_directory();
    snapshot_tracker_->set_recording_directory(params_->log_path.string());

    // Create the topic subscriptions and handlers
    for (TopicConfig& config : params_->topic_configs) {
        if (config.topic_name.find('*') != std::string::npos) {
            RCLCPP_INFO_STREAM(get_logger(), "Requesting subscription to topics matching " << config.topic_name);

            topic_patterns_.push_back(
                TopicPattern{ config.topic_name, compileTopicPattern(config.topic_name), config.throttle_count });
            continue;
        }

        RCLCPP_INFO_STREAM(get_logger(), "Requesting subscription to topic " << config.topic_name);

        LoggedTopic& logged_topic = topics_[config.topic_name];
        logged_topic.topic_name = config.topic_name;
        logged_topic.max_throttle = config.throttle_count;
    }

    // Create the discovery timer
    discovery_timer_ = create_timer(1s, std::bind(&LoggerNode::discoverTopics, this));

    // Create the metadata topic subscription
    metadata_sub_ = create_subscription<logging_msgs::msg::LogMetadata>(
        "log/metadata", rclcpp::SystemDefaultsQoS(), std::bind(&LoggerNode::metadataCallback, this, _1));

    // create the snapshot server
    snapshot_service_ = create_service<logging_msgs::srv::SnapshotRequest>(
        "log/snapshot", std::bind(&LoggerNode::handleSnapshotRequest, this, _1, _2));

    // Create the status publisher. Transient local, so a subscriber that joins between two statuses
    // still gets the latest one straight away.
    status_pub_ = create_publisher<logging_msgs::msg::LoggerStatus>("log/logger_status",
                                                                    rclcpp::QoS(1).reliable().transient_local());

    // Create the disk check timer
    disk_check_timer_ = create_timer(std::chrono::duration<double>(params_->disk_check_interval),
                                     std::bind(&LoggerNode::checkDiskUsage, this));

    // Create the split timer — fires at the configured split interval to guarantee
    // time-based splits happen even when no messages are flowing through write()
    split_timer_ =
        create_timer(std::chrono::duration<double>(params_->log_split_time), [this]() { log_writer_->check_split(); });

    // Open the log file
    try {
        openNextLogfile();
    } catch (const std::exception& e) {
        RCLCPP_ERROR_STREAM(get_logger(), "Failed to open log file: " << e.what());
        throw;
    }

    // Close the snapshots a previous run left open, and pick up its event counters. Recovery runs on wall-clock
    // time, as the snapshots it ends were stamped with it
    for (const std::string& snapshot_id : snapshot_tracker_->recover(rclcpp::Clock(RCL_SYSTEM_TIME).now())) {
        RCLCPP_INFO_STREAM(get_logger(), "WAL recovery: closed snapshot '" << snapshot_id << "'");
        emitSnapshotManifest(snapshot_id);
    }
    for (const auto& [event_name, counter] : snapshot_tracker_->event_counters()) {
        RCLCPP_INFO_STREAM(get_logger(), "Event '" << event_name << "': next snapshot ID will be " << counter);
    }

    // Log configured event snapshots
    if (!params_->event_snapshot_configs.empty()) {
        RCLCPP_INFO(get_logger(), "Event snapshot triggers configured:");
        for (const auto& cfg : params_->event_snapshot_configs) {
            RCLCPP_INFO_STREAM(get_logger(), "  - " << cfg.event_name << " (before: " << cfg.time_before
                                                    << "s, after: " << cfg.time_after << "s)");
        }
    }

    RCLCPP_INFO_STREAM(get_logger(), "Started " << get_name());
}

LoggerNode::~LoggerNode() {
    RCLCPP_INFO(get_logger(), "Logger node destructor called");
    shutdown();
}

void LoggerNode::shutdown() {
    RCLCPP_INFO(get_logger(), "Shutting down logger node...");

    // Cancel all timers to stop callbacks
    if (discovery_timer_) {
        discovery_timer_->cancel();
    }
    if (disk_check_timer_) {
        disk_check_timer_->cancel();
    }
    if (split_timer_) {
        split_timer_->cancel();
    }

    // Finalize any pending event snapshots, cut short at shutdown, and hand them to the uploader while the log
    // writer can still close out the split they end in
    if (snapshot_tracker_) {
        for (const std::string& snapshot_id :
             snapshot_tracker_->finalize_pending(rclcpp::Clock(RCL_SYSTEM_TIME).now())) {
            RCLCPP_INFO_STREAM(get_logger(),
                               "Finalized pending event snapshot '" << snapshot_id << "' during shutdown");
            emitSnapshotManifest(snapshot_id);
        }

        RCLCPP_INFO(get_logger(), "Saving snapshot metadata...");
        snapshot_tracker_->save();
    }

    // Close the log writer
    if (log_writer_) {
        RCLCPP_INFO(get_logger(), "Closing log writer...");
        log_writer_->close();
    }

    RCLCPP_INFO(get_logger(), "Logger node shutdown complete");
}

void LoggerNode::discoverTopics() {
    // Get the map of known topics and types
    std::map<std::string, std::vector<std::string>> topic_types_map = get_topic_names_and_types();

    for (const auto& [topic_name, topic_types] : topic_types_map) {
        // Find the requested topic this is, either named outright or matching a pattern
        auto found = topics_.find(topic_name);
        if (found != topics_.end() && found->second.subscription != nullptr) {
            continue;
        }

        const TopicPattern* matched_pattern = nullptr;
        if (found == topics_.end()) {
            for (const TopicPattern& pattern : topic_patterns_) {
                if (std::regex_match(topic_name, pattern.regex)) {
                    matched_pattern = &pattern;
                    break;
                }
            }
            if (matched_pattern == nullptr) {
                continue;
            }

            found = topics_.emplace(topic_name, LoggedTopic{}).first;
            found->second.topic_name = topic_name;
            found->second.max_throttle = matched_pattern->max_throttle;
        }

        // Configure the topic. A type whose typesupport can't be loaded throws; leave that topic unsubscribed
        // rather than take the logger down, and try it again next pass
        LoggedTopic& logged_topic = found->second;
        logged_topic.topic_type = topic_types.at(0);
        logged_topic.callback = std::bind(&LoggerNode::genericCallback, this, _1, std::ref(logged_topic));
        try {
            logged_topic.subscription = create_generic_subscription(logged_topic.topic_name, logged_topic.topic_type,
                                                                    rclcpp::SystemDefaultsQoS(), logged_topic.callback);
        } catch (const std::exception& e) {
            RCLCPP_ERROR_STREAM_THROTTLE(get_logger(), *get_clock(), 60000,
                                         "Failed to subscribe to topic " << logged_topic.topic_name << " ("
                                                                         << logged_topic.topic_type
                                                                         << "): " << e.what());
            continue;
        }

        if (matched_pattern != nullptr) {
            RCLCPP_INFO_STREAM(get_logger(), "Subscribed to topic " << logged_topic.topic_name << " (pattern "
                                                                    << matched_pattern->pattern << ")");
        } else {
            RCLCPP_INFO_STREAM(get_logger(), "Subscribed to topic " << logged_topic.topic_name);
        }
    }

    // A pattern can match topics that appear at any time, so discovery only ends when there are none and every
    // named topic is subscribed
    if (!topic_patterns_.empty()) {
        return;
    }
    for (const auto& [topic_name, logged_topic] : topics_) {
        if (logged_topic.subscription == nullptr) {
            return;
        }
    }
    discovery_timer_->cancel();
    RCLCPP_INFO(get_logger(), "All topics discovered successfully. Disabling discovery system");
}

void LoggerNode::genericCallback(std::shared_ptr<const rclcpp::SerializedMessage> msg, LoggedTopic& topic) {
    rclcpp::Time time_stamp = this->now();

    RCLCPP_DEBUG_STREAM(get_logger(), "Topic " << topic.topic_name << " received new data");

    topic.throttle_count++;

    if (topic.throttle_count > topic.max_throttle) {
        topic.throttle_count = 0;

        log_writer_->write(msg, topic.topic_name, topic.topic_type, time_stamp);
    }
}

void LoggerNode::metadataCallback(logging_msgs::msg::LogMetadata::ConstSharedPtr msg) {
    RCLCPP_INFO_STREAM(get_logger(), "Received metadata event: " << msg->key_name);

    // Get timestamp - use header stamp if available (for timed events), otherwise use current time
    rclcpp::Time event_time = msg->timed_event && msg->header.stamp.sec != 0 ? rclcpp::Time(msg->header.stamp) : now();

    // Write the metadata event to the log as a special channel
    log_writer_->write_metadata_event(*msg, event_time);

    // Check if this is a timed event that should trigger a snapshot
    if (msg->timed_event) {
        handleEventSnapshot(msg->key_name, event_time);
    }
}

void LoggerNode::openNextLogfile() {
    std::string log_name = log_manager_->next_log_name(params_->log_prefix);

    RCLCPP_INFO_STREAM(get_logger(), "Opening new log " << log_name << " in " << params_->log_path);

    log_writer_->open(log_util::make_mcap_storage_options(params_->log_path.string(), params_->log_split_time,
                                                          params_->log_split_size),
                      rosbag2_cpp::ConverterOptions{ "cdr", "cdr" }, log_name);
}

void LoggerNode::handleSnapshotRequest(logging_msgs::srv::SnapshotRequest::Request::SharedPtr req,
                                       logging_msgs::srv::SnapshotRequest::Response::SharedPtr resp) {
    // Figure out if this is a start or stop request
    if (req->request_id == req->REQUEST_START) {
        if (snapshot_tracker_->has_snapshot(req->snapshot_name)) {
            // snapshot exists, tell the caller
            resp->resp_code = resp->RESPONSE_SNAPSHOT_EXISTS;
            resp->response_msg = "Snapshot " + req->snapshot_name + " already exists";
        } else {
            RCLCPP_INFO_STREAM(get_logger(), "Started snapshot " + req->snapshot_name);

            bool success = snapshot_tracker_->start_manual(req->snapshot_name, req->description, req->tags);
            if (success) {
                resp->resp_code = resp->RESPONSE_OK;
                resp->response_msg = "Snapshot " + req->snapshot_name + " started";
            } else {
                resp->resp_code = resp->RESPONSE_REQ_INVALID;
                resp->response_msg = "Failed to start snapshot " + req->snapshot_name;
            }
        }
    } else if (req->request_id == req->REQUEST_STOP) {
        if (!snapshot_tracker_->is_running(req->snapshot_name)) {
            resp->resp_code = resp->RESPONSE_INVALID_NAME;
            resp->response_msg = "Snapshot " + req->snapshot_name + " does not exist or is not running";
        } else {
            RCLCPP_INFO_STREAM(get_logger(), "Finished snapshot " + req->snapshot_name);

            bool success = snapshot_tracker_->stop_manual(req->snapshot_name);
            if (success) {
                emitSnapshotManifest(req->snapshot_name);
                resp->resp_code = resp->RESPONSE_OK;
                resp->response_msg = "Snapshot " + req->snapshot_name + " completed";
            } else {
                resp->resp_code = resp->RESPONSE_REQ_INVALID;
                resp->response_msg = "Failed to stop snapshot " + req->snapshot_name;
            }
        }
    } else if (req->request_id == req->REQUEST_NONE) {
        resp->resp_code = resp->RESPONSE_OK;
        resp->response_msg = "PONG";
    } else {
        resp->resp_code = resp->RESPONSE_REQ_INVALID;
        resp->response_msg = "Request ID does not exist!";
    }
}

void LoggerNode::checkDiskUsage() {
    log_util::DiskCheckResult disk = log_manager_->check_disk(log_writer_->get_current_file_path());

    if (disk.rotated_files > 0) {
        RCLCPP_INFO_STREAM(get_logger(), "Rotated " << disk.rotated_files << " old log files to free disk space");
    }

    if (disk.rotation_blocked) {
        RCLCPP_WARN_STREAM_THROTTLE(get_logger(), *get_clock(), 60000,
                                    "Disk usage is " << disk.disk_usage_percent << "%, at or above the "
                                                     << params_->rotation_threshold_percent
                                                     << "% rotation threshold, but every remaining log is protected "
                                                        "by a snapshot");
    }

    if (disk.snapshot_usage_high) {
        std::ostringstream desc;
        desc << std::fixed << std::setprecision(1) << "Disk almost full: snapshot-protected logs use "
             << disk.snapshot_usage_percent << "% of the disk, at or above the "
             << params_->snapshot_usage_threshold_percent << "% snapshot threshold (total usage "
             << disk.disk_usage_percent << "%). Upload or delete snapshots to free space";
        sendFaultReport(DISK_FAULT_SYSTEM, DISK_FAULT_NAME,
                        std::chrono::duration<double>(DISK_FAULT_TOV_INTERVALS * params_->disk_check_interval),
                        desc.str());
    }
}

void LoggerNode::handleEventSnapshot(const std::string& event_name, const rclcpp::Time& event_time) {
    // Check if this event should trigger a snapshot. Only one snapshot is made per event occurrence
    for (const auto& config : params_->event_snapshot_configs) {
        if (config.event_name != event_name) {
            continue;
        }

        RCLCPP_INFO_STREAM(get_logger(), "Event '" << event_name << "' triggered automatic snapshot creation");

        // Replace {event_name} in description template
        std::string description = config.description;
        size_t pos = description.find("{event_name}");
        if (pos != std::string::npos) {
            description.replace(pos, 12, event_name);
        }

        std::string snapshot_id = snapshot_tracker_->start_event(event_name, event_time, config.time_before,
                                                                 config.time_after, description, config.tags);
        if (snapshot_id.empty()) {
            RCLCPP_WARN_STREAM(get_logger(), "Failed to start event snapshot for '" << event_name << "'");
        } else {
            RCLCPP_INFO_STREAM(get_logger(), "Started event snapshot '" << snapshot_id << "' (will finalize at "
                                                                        << event_time.seconds() + config.time_after
                                                                        << "s)");
        }
        break;
    }
}

void LoggerNode::onTick() {
    rclcpp::Time now;
    try {
        now = this->now();
    } catch (const std::exception& e) {
        return;
    }

    // Finalize the event snapshots whose time_after window has ended
    for (const std::string& snapshot_id : snapshot_tracker_->finalize_due(now)) {
        RCLCPP_INFO_STREAM(get_logger(), "Finalized event snapshot '" << snapshot_id << "'");
        emitSnapshotManifest(snapshot_id);
    }

    status_pub_->publish(buildStatus(now));
}

logging_msgs::msg::LoggerStatus LoggerNode::buildStatus(const rclcpp::Time& now) const {
    logging_msgs::msg::LoggerStatus status;
    status.stamp = now;

    // The percentage is the one the rotation decision uses; the byte counts are the same statvfs numbers
    // (free is f_bfree, not f_bavail). Snapshot usage is as of the last disk check, as measuring it rescans the
    // log directory.
    log_util::DiskCheckResult disk = log_manager_->get_last_disk_check();
    status.log_path = params_->log_path.string();
    status.disk_usage_percent = log_manager_->get_disk_usage_percent();
    status.disk_rotation_threshold_percent = params_->rotation_threshold_percent;
    status.snapshot_usage_threshold_percent = params_->snapshot_usage_threshold_percent;
    status.snapshot_usage_percent = disk.snapshot_usage_percent;
    status.snapshot_protected_bytes = disk.protected_bytes;
    std::error_code error;
    std::filesystem::space_info space = std::filesystem::space(params_->log_path, error);
    if (!error) {
        status.disk_total_bytes = space.capacity;
        status.disk_free_bytes = space.free;
    }

    status.events = makeEventStatuses(params_->event_snapshot_configs, snapshot_tracker_->event_counters());

    std::vector<log_util::SnapshotInfo> snapshots = snapshot_tracker_->get_all_snapshots();
    status.total_snapshot_count = static_cast<uint32_t>(snapshots.size());
    status.snapshots = makeSnapshotStatuses(std::move(snapshots), snapshot_tracker_->pending_event_ids());

    return status;
}

void LoggerNode::emitSnapshotManifest(const std::string& snapshot_id) {
    // Force the log writer to close the current MCAP file before we hand off the
    // manifest to the uploader. This guarantees the snapshot data is in a
    // readable, properly-finalised file rather than the still-open active split.
    if (log_writer_ && log_writer_->is_open()) {
        log_writer_->force_split();
    }

    log_util::SnapshotInfo snapshot = snapshot_tracker_->get_snapshot(snapshot_id);
    if (snapshot.id.empty()) {
        RCLCPP_WARN_STREAM(get_logger(), "Cannot emit manifest for unknown snapshot: " << snapshot_id);
        return;
    }

    // Create manifest directory if it doesn't exist
    if (!std::filesystem::exists(manifest_dir_)) {
        try {
            std::filesystem::create_directories(manifest_dir_);
        } catch (const std::filesystem::filesystem_error& e) {
            RCLCPP_ERROR_STREAM(get_logger(), "Failed to create manifest directory: " << e.what());
            return;
        }
    }

    int64_t created_at =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    YAML::Emitter out;
    out << YAML::BeginMap;
    out << YAML::Key << "version" << YAML::Value << 1;
    out << YAML::Key << "snapshot_id" << YAML::Value << snapshot.id;
    out << YAML::Key << "trigger" << YAML::Value << snapshot.trigger;
    out << YAML::Key << "description" << YAML::Value << snapshot.description;
    out << YAML::Key << "tags" << YAML::Value << YAML::BeginSeq;
    for (const auto& tag : snapshot.tags) {
        out << tag;
    }
    out << YAML::EndSeq;
    out << YAML::Key << "start_time" << YAML::Value << snapshot.start_time;
    out << YAML::Key << "end_time" << YAML::Value << snapshot.end_time;
    out << YAML::Key << "log_directory" << YAML::Value << params_->log_path.string();
    out << YAML::Key << "created_at" << YAML::Value << created_at;
    out << YAML::EndMap;

    std::filesystem::path tmp_path = manifest_dir_ / (snapshot_id + ".manifest.tmp");
    std::filesystem::path final_path = manifest_dir_ / (snapshot_id + ".manifest");

    const char* content = out.c_str();
    size_t len = std::strlen(content);

    int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        RCLCPP_ERROR_STREAM(get_logger(), "Failed to open manifest tmp file: " << tmp_path);
        return;
    }

    ssize_t written = ::write(fd, content, len);
    ::fdatasync(fd);
    ::close(fd);

    if (written != static_cast<ssize_t>(len)) {
        RCLCPP_ERROR_STREAM(get_logger(), "Partial write to manifest tmp file: " << tmp_path);
        return;
    }

    try {
        std::filesystem::rename(tmp_path, final_path);
        RCLCPP_INFO_STREAM(get_logger(), "Emitted snapshot manifest: " << final_path);
    } catch (const std::filesystem::filesystem_error& e) {
        RCLCPP_ERROR_STREAM(get_logger(), "Failed to rename manifest: " << e.what());
    }
}

}  // namespace basin::continuous_logger
