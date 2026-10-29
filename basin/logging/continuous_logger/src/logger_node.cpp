#include "continous_logger/logger_node.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <chrono>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <regex>
#include <set>
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

struct LogInfo {
    ///< Name of the directory
    std::string name;

    ///< Last updated time for the folder
    std::filesystem::file_time_type updated;

    bool operator<(const LogInfo& other) const { return updated < other.updated; }
};

LoggerNode::LoggerNode(const rclcpp::NodeOptions& options) : basin::node_core::NodeCore("logger", "", options) {
    params_ = std::make_unique<LoggerParams>(this);

    // Create the component instances
    log_writer_ = std::make_unique<log_util::LogWriter>();
    snapshot_writer_ = std::make_unique<log_util::SnapshotWriter>();
    log_manager_ = std::make_unique<log_util::LogManager>();

    // Configure log manager
    log_manager_->set_log_directory(params_->log_path.string());
    log_manager_->set_disk_limits(params_->rotation_threshold_percent, params_->disk_warning_threshold_percent);

    // Resolve manifest directory (default: {log_path}/pending)
    manifest_dir_ = params_->manifest_dir.empty() ? params_->log_path / "pending" : params_->manifest_dir;
    log_manager_->set_manifest_dir(manifest_dir_.string());

    // Scan existing logs
    log_manager_->scan_log_directory();

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

    // Initialize event snapshot counters from existing snapshots
    recoverFromWAL();
    initializeEventSnapshotCounters();

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
    if (event_snapshot_timer_) {
        event_snapshot_timer_->cancel();
    }
    if (split_timer_) {
        split_timer_->cancel();
    }

    // Finalize any pending event snapshots
    {
        std::lock_guard<std::mutex> lock(event_snapshot_mutex_);

        auto now_sec =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();

        for (const auto& pending : pending_event_snapshots_) {
            RCLCPP_INFO_STREAM(get_logger(),
                               "Finalizing pending event snapshot '" << pending.snapshot_id << "' during shutdown");
            snapshot_writer_->stop_snapshot(pending.snapshot_id, now_sec);
        }
        pending_event_snapshots_.clear();

        // Clear WAL now that all pending state is resolved
        saveWalLocked();
    }

    // Save snapshot metadata to disk
    if (snapshot_writer_) {
        RCLCPP_INFO(get_logger(), "Saving snapshot metadata...");
        snapshot_writer_->save_to_file();
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
            logged_topic.subscription = create_generic_subscription(
                logged_topic.topic_name, logged_topic.topic_type, rclcpp::SystemDefaultsQoS(), logged_topic.callback);
        } catch (const std::exception& e) {
            RCLCPP_ERROR_STREAM_THROTTLE(get_logger(), *get_clock(), 60000,
                                         "Failed to subscribe to topic " << logged_topic.topic_name << " ("
                                                                         << logged_topic.topic_type << "): " << e.what());
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
    // Create the log directory if it doesnt exist
    if (!std::filesystem::exists(params_->log_path)) {
        std::filesystem::create_directories(params_->log_path);
    }

    // If it does exist but is not a directory this is an error that must be handled.
    if (!std::filesystem::is_directory(params_->log_path)) {
        RCLCPP_FATAL_STREAM(get_logger(), "Configured log path is not a valid directory! " << params_->log_path);
        throw std::runtime_error("Log path is not a directory!");
    }

    RCLCPP_INFO_STREAM(get_logger(), "Logging to folder: " << params_->log_path);

    // Scan the log directory for existing MCAP files to determine next sequence number
    std::vector<LogInfo> prior_logs;
    for (const std::filesystem::path& possible_log_file : std::filesystem::directory_iterator(params_->log_path)) {
        if (std::filesystem::is_regular_file(possible_log_file) && possible_log_file.extension() == ".mcap") {
            // Skip empty/corrupted files (likely from crashed previous runs)
            auto file_size = std::filesystem::file_size(possible_log_file);
            if (file_size == 0) {
                RCLCPP_WARN_STREAM(get_logger(), "Skipping empty/corrupted log file: " << possible_log_file.filename());
                continue;
            }

            LogInfo discovered_log{ possible_log_file.filename(), std::filesystem::last_write_time(possible_log_file) };

            RCLCPP_DEBUG_STREAM(get_logger(), "Discovered log: " << discovered_log.name);

            prior_logs.push_back(discovered_log);
        }
    }

    // discover the previous sequence #
    std::sort(prior_logs.begin(), prior_logs.end());

    size_t next_log_number = 0;
    if (!prior_logs.empty()) {
        std::string last_log_name = prior_logs.at(prior_logs.size() - 1).name;
        RCLCPP_INFO_STREAM(get_logger(), "Last logfile detected " << last_log_name);

        std::regex log_num_regex{ "\\d+" };
        std::smatch log_num_match;
        if (std::regex_search(last_log_name, log_num_match, log_num_regex)) {
            // always take the first
            next_log_number = std::stoull(log_num_match[0].str()) + 1;
        } else {
            RCLCPP_WARN_STREAM(get_logger(), "Unable to detect prior log number from log name " << last_log_name);
        }
    }

    // build the new log file name (will create log_prefix_N.mcap files)
    std::string log_name = params_->log_prefix + std::to_string(next_log_number);

    RCLCPP_INFO_STREAM(get_logger(), "Opening new log " << log_name);

    // Configure snapshot writer with the log directory
    snapshot_writer_->set_recording_directory(params_->log_path.string());
    snapshot_writer_->load_from_file();

    // Ensure snapshots.yaml file exists even if empty
    // This makes it easier for tools to validate the log directory
    snapshot_writer_->ensure_file_exists();

    // open the next log in the sequence
    // Use the log_path directly as the URI for flat directory structure
    rosbag2_storage::StorageOptions options;
    options.uri = params_->log_path.string();
    options.max_bagfile_duration = params_->log_split_time;
    options.max_bagfile_size = params_->log_split_size;
    options.storage_id = "mcap";      // Use MCAP format as specified in the design
    options.storage_config_uri = "";  // No additional config needed

    // Set converter options for serialization
    rosbag2_cpp::ConverterOptions converter_options;
    converter_options.input_serialization_format = "cdr";   // Input is always CDR from ROS2
    converter_options.output_serialization_format = "cdr";  // Output is also CDR for MCAP

    log_writer_->open(options, converter_options, log_name);
}

void LoggerNode::handleSnapshotRequest(logging_msgs::srv::SnapshotRequest::Request::SharedPtr req,
                                       logging_msgs::srv::SnapshotRequest::Response::SharedPtr resp) {
    // Figure out if this is a start or stop request
    if (req->request_id == req->REQUEST_START) {
        if (snapshot_writer_->has_snapshot(req->snapshot_name)) {
            // snapshot exists, tell the caller
            resp->resp_code = resp->RESPONSE_SNAPSHOT_EXISTS;
            resp->response_msg = "Snapshot " + req->snapshot_name + " already exists";
        } else {
            RCLCPP_INFO_STREAM(get_logger(), "Started snapshot " + req->snapshot_name);

            bool success = snapshot_writer_->start_snapshot(req->snapshot_name, "manual", req->description, req->tags);
            if (success) {
                saveWal();
                resp->resp_code = resp->RESPONSE_OK;
                resp->response_msg = "Snapshot " + req->snapshot_name + " started";
            } else {
                resp->resp_code = resp->RESPONSE_REQ_INVALID;
                resp->response_msg = "Failed to start snapshot " + req->snapshot_name;
            }
        }
    } else if (req->request_id == req->REQUEST_STOP) {
        if (!snapshot_writer_->is_snapshot_running(req->snapshot_name)) {
            resp->resp_code = resp->RESPONSE_INVALID_NAME;
            resp->response_msg = "Snapshot " + req->snapshot_name + " does not exist or is not running";
        } else {
            RCLCPP_INFO_STREAM(get_logger(), "Finished snapshot " + req->snapshot_name);

            bool success = snapshot_writer_->stop_snapshot(req->snapshot_name);
            if (success) {
                saveWal();
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
    // Rescan log directory to get updated info
    log_manager_->scan_log_directory();

    // Check if we should warn about disk usage
    if (log_manager_->should_warn_disk_usage()) {
        RCLCPP_WARN_STREAM(get_logger(), "Disk usage is high: " << log_manager_->get_disk_usage_percent() << "%");
    }

    // Try to rotate logs if needed
    if (log_manager_->check_and_rotate_logs()) {
        RCLCPP_INFO(get_logger(), "Rotated old log files to free disk space");
    }

    // Judge the disk after rotating: if that freed enough, there is nothing left to report
    if (log_manager_->should_warn_disk_usage()) {
        std::ostringstream desc;
        desc << std::fixed << std::setprecision(1) << "Disk usage is " << log_manager_->get_disk_usage_percent()
             << "%, at or above the " << params_->disk_warning_threshold_percent << "% warning threshold. Old logs are "
             << "rotated away once it reaches " << params_->rotation_threshold_percent
             << "%, except those snapshots protect";
        sendFaultReport(DISK_FAULT_SYSTEM, DISK_FAULT_NAME,
                        std::chrono::duration<double>(DISK_FAULT_TOV_INTERVALS * params_->disk_check_interval),
                        desc.str());
    }
}

void LoggerNode::handleEventSnapshot(const std::string& event_name, const rclcpp::Time& event_time) {
    // Check if this event should trigger a snapshot
    for (const auto& config : params_->event_snapshot_configs) {
        if (config.event_name == event_name) {
            RCLCPP_INFO_STREAM(get_logger(), "Event '" << event_name << "' triggered automatic snapshot creation");

            // Generate unique snapshot ID using per-event counter
            std::lock_guard<std::mutex> lock(event_snapshot_mutex_);

            // Get or initialize counter for this event name
            uint64_t snap_id = event_snapshot_counters_[event_name]++;
            std::string snapshot_id = "event_" + event_name + "_" + std::to_string(snap_id);

            // Calculate start time (event_time - time_before)
            int64_t start_time_sec = event_time.seconds() - static_cast<int64_t>(config.time_before);

            // Calculate finalize time (event_time + time_after)
            rclcpp::Time finalize_time = event_time + rclcpp::Duration::from_seconds(config.time_after);

            // Replace {event_name} in description template
            std::string description = config.description;
            size_t pos = description.find("{event_name}");
            if (pos != std::string::npos) {
                description.replace(pos, 12, event_name);
            }

            // Create trigger string
            std::string trigger = "event-" + event_name;

            // Start the snapshot with the calculated start time
            bool success =
                snapshot_writer_->start_snapshot(snapshot_id, trigger, description, config.tags, start_time_sec);

            if (success) {
                // Add to pending snapshots for finalization
                PendingEventSnapshot pending;
                pending.snapshot_id = snapshot_id;
                pending.event_name = event_name;
                pending.trigger_time = event_time;
                pending.finalize_time = finalize_time;
                pending.tags = config.tags;
                pending.description = description;

                pending_event_snapshots_.push_back(pending);

                saveWalLocked();

                RCLCPP_INFO_STREAM(get_logger(), "Started event snapshot '" << snapshot_id << "' (will finalize at "
                                                                            << finalize_time.seconds() << "s)");
            } else {
                RCLCPP_WARN_STREAM(get_logger(), "Failed to start event snapshot for '" << event_name << "'");
            }

            // Only create one snapshot per event occurrence
            break;
        }
    }
}

void LoggerNode::onTick() {
    std::lock_guard<std::mutex> lock(event_snapshot_mutex_);

    rclcpp::Time now;
    try {
        now = this->now();
    } catch (const std::exception& e) {
        return;
    }

    // Check for snapshots that need to be finalized
    auto it = pending_event_snapshots_.begin();
    while (it != pending_event_snapshots_.end()) {
        if (now >= it->finalize_time) {
            // Time to finalize this snapshot
            int64_t end_time_sec = it->finalize_time.seconds();

            bool success = snapshot_writer_->stop_snapshot(it->snapshot_id, end_time_sec);

            if (success) {
                RCLCPP_INFO_STREAM(get_logger(), "Finalized event snapshot '" << it->snapshot_id << "'");
                std::string snap_id = it->snapshot_id;
                it = pending_event_snapshots_.erase(it);
                saveWalLocked();
                emitSnapshotManifest(snap_id);
            } else {
                RCLCPP_WARN_STREAM(get_logger(), "Failed to finalize event snapshot '" << it->snapshot_id << "'");
                it = pending_event_snapshots_.erase(it);
            }
        } else {
            ++it;
        }
    }

    status_pub_->publish(buildStatusLocked(now));
}

logging_msgs::msg::LoggerStatus LoggerNode::buildStatusLocked(const rclcpp::Time& now) const {
    logging_msgs::msg::LoggerStatus status;
    status.stamp = now;

    // The percentage is the one the rotation decision uses; the byte counts are the same statvfs numbers
    // (free is f_bfree, not f_bavail).
    status.log_path = params_->log_path.string();
    status.disk_usage_percent = log_manager_->get_disk_usage_percent();
    status.disk_rotation_threshold_percent = params_->rotation_threshold_percent;
    status.disk_warning_threshold_percent = params_->disk_warning_threshold_percent;
    std::error_code error;
    std::filesystem::space_info space = std::filesystem::space(params_->log_path, error);
    if (!error) {
        status.disk_total_bytes = space.capacity;
        status.disk_free_bytes = space.free;
    }

    status.events = makeEventStatuses(params_->event_snapshot_configs, event_snapshot_counters_);

    std::set<std::string> pending_ids;
    for (const PendingEventSnapshot& pending : pending_event_snapshots_) {
        pending_ids.insert(pending.snapshot_id);
    }
    std::vector<log_util::SnapshotInfo> snapshots = snapshot_writer_->get_all_snapshots();
    status.total_snapshot_count = static_cast<uint32_t>(snapshots.size());
    status.snapshots = makeSnapshotStatuses(std::move(snapshots), pending_ids);

    return status;
}

void LoggerNode::initializeEventSnapshotCounters() {
    std::lock_guard<std::mutex> lock(event_snapshot_mutex_);

    // Get all existing snapshots from the snapshot writer
    auto snapshots = snapshot_writer_->get_all_snapshots();

    // Parse snapshot IDs to find event-triggered snapshots and update counters
    for (const auto& snapshot : snapshots) {
        // Check if this is an event-triggered snapshot (format: event_<name>_<number>)
        if (snapshot.id.rfind("event_", 0) == 0) {  // Starts with "event_"
            // Extract the event name and counter
            std::string id_suffix = snapshot.id.substr(6);  // Remove "event_" prefix

            // Find the last underscore (separates name from counter)
            size_t last_underscore = id_suffix.rfind('_');
            if (last_underscore != std::string::npos) {
                std::string event_name = id_suffix.substr(0, last_underscore);
                std::string counter_str = id_suffix.substr(last_underscore + 1);

                try {
                    uint64_t counter = std::stoull(counter_str);

                    // Update the counter for this event name to be one more than the highest seen
                    if (event_snapshot_counters_[event_name] <= counter) {
                        event_snapshot_counters_[event_name] = counter + 1;
                    }
                } catch (const std::exception& e) {
                    // Invalid counter format, skip this snapshot
                    RCLCPP_WARN_STREAM(get_logger(), "Failed to parse counter from snapshot ID: " << snapshot.id);
                }
            }
        }
    }

    // Log the initialized counters
    if (!event_snapshot_counters_.empty()) {
        RCLCPP_INFO(get_logger(), "Initialized event snapshot counters from existing snapshots:");
        for (const auto& [event_name, counter] : event_snapshot_counters_) {
            RCLCPP_INFO_STREAM(get_logger(), "  - " << event_name << ": next ID will be " << counter);
        }
    }
}

void LoggerNode::recoverFromWAL() {
    log_util::WalState wal_state;
    if (!snapshot_writer_->load_wal(wal_state)) {
        return;  // No WAL — nothing to recover
    }

    std::lock_guard<std::mutex> lock(event_snapshot_mutex_);

    int64_t now_sec =
        std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();

    // Recover open manual snapshots: stop them and emit manifests
    for (const auto& open_snap : wal_state.open_snapshots) {
        if (!snapshot_writer_->is_snapshot_running(open_snap.id)) {
            continue;  // Already closed (shouldn't happen, but be safe)
        }
        RCLCPP_INFO_STREAM(get_logger(), "WAL recovery: closing open snapshot '" << open_snap.id << "'");
        bool success = snapshot_writer_->stop_snapshot(open_snap.id, now_sec);
        if (success) {
            emitSnapshotManifest(open_snap.id);
        }
    }
    wal_state.open_snapshots.clear();

    // Recover pending event snapshots
    std::vector<log_util::WalPendingEventSnapshot> still_pending;
    for (const auto& wal_pending : wal_state.pending_event_snapshots) {
        if (wal_pending.finalize_time_sec <= now_sec) {
            // Finalize time has already passed
            RCLCPP_INFO_STREAM(get_logger(),
                               "WAL recovery: finalizing overdue event snapshot '" << wal_pending.snapshot_id << "'");
            bool success = snapshot_writer_->stop_snapshot(wal_pending.snapshot_id, wal_pending.finalize_time_sec);
            if (success) {
                emitSnapshotManifest(wal_pending.snapshot_id);
            }
        } else {
            // Finalize time is still in the future — re-add to in-memory list
            PendingEventSnapshot mem_pending;
            mem_pending.snapshot_id = wal_pending.snapshot_id;
            mem_pending.event_name = wal_pending.event_name;
            mem_pending.finalize_time = rclcpp::Time(
                wal_pending.finalize_time_sec, static_cast<uint32_t>(wal_pending.finalize_time_nsec), RCL_SYSTEM_TIME);
            pending_event_snapshots_.push_back(mem_pending);
            still_pending.push_back(wal_pending);
        }
    }
    wal_state.pending_event_snapshots = still_pending;

    // Persist the updated (partially-resolved) WAL
    snapshot_writer_->save_wal(wal_state);
}

void LoggerNode::emitSnapshotManifest(const std::string& snapshot_id) {
    // Force the log writer to close the current MCAP file before we hand off the
    // manifest to the uploader. This guarantees the snapshot data is in a
    // readable, properly-finalised file rather than the still-open active split.
    if (log_writer_ && log_writer_->is_open()) {
        log_writer_->force_split();
    }

    log_util::SnapshotInfo snapshot = snapshot_writer_->get_snapshot(snapshot_id);
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

void LoggerNode::saveWalLocked() {
    // Must be called with event_snapshot_mutex_ held
    log_util::WalState wal;

    // Collect running manual snapshots (trigger == "manual", not yet stopped)
    for (const auto& snapshot : snapshot_writer_->get_all_snapshots()) {
        if (snapshot.end_time == -1 && snapshot.trigger == "manual") {
            log_util::WalOpenSnapshot ws;
            ws.id = snapshot.id;
            ws.start_time = snapshot.start_time;
            ws.trigger = snapshot.trigger;
            ws.description = snapshot.description;
            ws.tags = snapshot.tags;
            wal.open_snapshots.push_back(ws);
        }
    }

    // Collect pending event snapshots
    for (const auto& pending : pending_event_snapshots_) {
        log_util::WalPendingEventSnapshot wps;
        wps.snapshot_id = pending.snapshot_id;
        wps.event_name = pending.event_name;
        wps.finalize_time_sec = static_cast<int64_t>(pending.finalize_time.seconds());
        wps.finalize_time_nsec = static_cast<int32_t>(pending.finalize_time.nanoseconds() % 1'000'000'000LL);
        wal.pending_event_snapshots.push_back(wps);
    }

    snapshot_writer_->save_wal(wal);
}

void LoggerNode::saveWal() {
    std::lock_guard<std::mutex> lock(event_snapshot_mutex_);
    saveWalLocked();
}

}  // namespace basin::continuous_logger

#include <rclcpp_components/register_node_macro.hpp>
RCLCPP_COMPONENTS_REGISTER_NODE(basin::continuous_logger::LoggerNode);
