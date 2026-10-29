#include "basin/log_util/log_manager.hpp"

#include <sys/statvfs.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <rosbag2_storage/storage_factory.hpp>
#include <rosbag2_storage/storage_options.hpp>

namespace basin::log_util {

LogManager::LogManager() : rotation_threshold_percent_(80.0), warning_threshold_percent_(70.0) {}

LogManager::~LogManager() {}

void LogManager::set_manifest_dir(const std::string& dir) {
    std::lock_guard<std::mutex> lock(mutex_);
    manifest_dir_ = dir;
}

void LogManager::set_log_directory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    log_directory_ = directory;
    snapshot_writer_.set_recording_directory(directory);
}

void LogManager::set_disk_limits(double rotation_threshold_percent, double warning_threshold_percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    rotation_threshold_percent_ = rotation_threshold_percent;
    warning_threshold_percent_ = warning_threshold_percent;
}

size_t LogManager::scan_log_directory() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (log_directory_.empty() || !std::filesystem::exists(log_directory_)) {
        return 0;
    }

    // Collect the set of non-empty MCAP files currently on disk
    std::set<std::string> current_files;
    for (const auto& entry : std::filesystem::directory_iterator(log_directory_)) {
        if (entry.is_regular_file() && entry.path().extension() == ".mcap") {
            if (std::filesystem::file_size(entry.path()) > 0) {
                current_files.insert(entry.path().string());
            }
        }
    }

    // Evict cache entries for files that have been removed
    for (auto it = scan_cache_.begin(); it != scan_cache_.end();) {
        if (current_files.find(it->first) == current_files.end()) {
            it = scan_cache_.erase(it);
        } else {
            ++it;
        }
    }

    log_files_.clear();
    for (const auto& file_path : current_files) {
        auto cache_it = scan_cache_.find(file_path);
        if (cache_it != scan_cache_.end()) {
            // Reuse cached time range; update mutable fields
            LogFileInfo info = cache_it->second;
            info.size_bytes = std::filesystem::file_size(file_path);
            info.associated_with_snapshot = false;  // will be refreshed below
            log_files_.push_back(info);
        } else {
            // New file — read time range via storage factory
            LogFileInfo info;
            info.path = file_path;
            info.last_write_time = std::filesystem::last_write_time(file_path);
            info.size_bytes = std::filesystem::file_size(file_path);
            info.associated_with_snapshot = false;
            if (get_log_time_range(info.path, info.start_time, info.end_time)) {
                scan_cache_[file_path] = info;
                log_files_.push_back(info);
            }
        }
    }

    // Load snapshots from file
    snapshot_writer_.load_from_file();

    // Scan manifest_dir_ for *.manifest and *.manifest.processing files
    manifest_protected_ranges_.clear();
    if (!manifest_dir_.empty() && std::filesystem::exists(manifest_dir_)) {
        for (const auto& entry : std::filesystem::directory_iterator(manifest_dir_)) {
            if (!entry.is_regular_file()) {
                continue;
            }
            const std::string fname = entry.path().filename().string();
            const std::string ext = entry.path().extension().string();
            const std::string processing_suffix = ".manifest.processing";
            bool is_manifest = (ext == ".manifest");
            bool is_processing = (fname.size() >= processing_suffix.size() &&
                                  fname.compare(fname.size() - processing_suffix.size(), processing_suffix.size(),
                                                processing_suffix) == 0);
            if (!is_manifest && !is_processing) {
                continue;
            }
            try {
                YAML::Node yaml = YAML::LoadFile(entry.path().string());
                if (yaml["start_time"] && yaml["end_time"]) {
                    int64_t start = yaml["start_time"].as<int64_t>();
                    int64_t end = yaml["end_time"].as<int64_t>();
                    manifest_protected_ranges_.push_back({ start, end });
                }
            } catch (...) {
                // Malformed manifest — skip silently
            }
        }
    }

    // Update snapshot / manifest associations
    for (auto& log_file : log_files_) {
        log_file.associated_with_snapshot = is_log_file_associated_with_snapshot(log_file);
        if (scan_cache_.count(log_file.path)) {
            scan_cache_[log_file.path].associated_with_snapshot = log_file.associated_with_snapshot;
        }
    }

    // Sort by last write time (oldest first)
    std::sort(log_files_.begin(), log_files_.end(),
              [](const LogFileInfo& a, const LogFileInfo& b) { return a.last_write_time < b.last_write_time; });

    return log_files_.size();
}

bool LogManager::check_and_rotate_logs() {
    std::lock_guard<std::mutex> lock(mutex_);

    double usage = get_disk_usage_percent();

    // Check if we need to delete logs
    if (usage < rotation_threshold_percent_) {
        return false;
    }

    // Try to delete oldest unprotected log
    return delete_oldest_unprotected_log();
}

std::vector<std::string> LogManager::get_log_files() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::string> result;
    result.reserve(log_files_.size());

    for (const auto& info : log_files_) {
        result.push_back(info.path);
    }

    return result;
}

std::vector<std::string> LogManager::get_snapshot_log_files(const std::string& snapshot_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    SnapshotInfo snapshot = snapshot_writer_.get_snapshot(snapshot_id);
    if (snapshot.id.empty()) {
        return {};
    }

    std::vector<std::string> result;

    // Find all MCAP files that overlap with the snapshot time range
    for (const auto& log_file : log_files_) {
        // Check for time range overlap
        if (!(log_file.end_time < snapshot.start_time || log_file.start_time > snapshot.end_time)) {
            result.push_back(log_file.path);
        }
    }

    return result;
}

double LogManager::get_disk_usage_percent() const {
    // Note: This should be called with mutex already locked or from const methods

    if (log_directory_.empty()) {
        return 0.0;
    }

    struct statvfs stat;
    if (statvfs(log_directory_.c_str(), &stat) != 0) {
        return 0.0;
    }

    uintmax_t total_space = stat.f_blocks * stat.f_frsize;
    uintmax_t free_space = stat.f_bfree * stat.f_frsize;
    uintmax_t used_space = total_space - free_space;

    return (static_cast<double>(used_space) / static_cast<double>(total_space)) * 100.0;
}

bool LogManager::should_warn_disk_usage() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return get_disk_usage_percent() >= warning_threshold_percent_;
}

const SnapshotWriter& LogManager::get_snapshot_writer() const { return snapshot_writer_; }

bool LogManager::is_log_file_associated_with_snapshot(const LogFileInfo& log_file) const {
    // Note: This should be called with mutex already locked

    // Check if log overlaps with any completed snapshot
    for (const auto& snapshot : snapshot_writer_.get_all_snapshots()) {
        // Skip running snapshots (end_time == -1)
        if (snapshot.end_time == -1) {
            continue;
        }

        // Check for time range overlap
        if (!(log_file.end_time < snapshot.start_time || log_file.start_time > snapshot.end_time)) {
            return true;
        }
    }

    // Check manifest-based protection windows
    for (const auto& range : manifest_protected_ranges_) {
        if (!(log_file.end_time < range.first || log_file.start_time > range.second)) {
            return true;
        }
    }

    return false;
}

bool LogManager::get_log_time_range(const std::string& log_path, int64_t& start_time, int64_t& end_time) const {
    // Note: This should be called with mutex already locked

    try {
        // Open the MCAP file using rosbag2's storage interface
        rosbag2_storage::StorageFactory factory;
        rosbag2_storage::StorageOptions options;
        options.uri = log_path;
        options.storage_id = "mcap";

        auto storage = factory.open_read_only(options);
        if (!storage) {
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
        return false;
    }
}

bool LogManager::delete_oldest_unprotected_log() {
    // Note: This should be called with mutex already locked

    // Find oldest log file not associated with snapshot
    for (auto it = log_files_.begin(); it != log_files_.end(); ++it) {
        if (!it->associated_with_snapshot) {
            try {
                std::filesystem::remove(it->path);
                log_files_.erase(it);
                return true;
            } catch (const std::filesystem::filesystem_error& e) {
                // Failed to delete, try next one
                continue;
            }
        }
    }

    return false;  // No unprotected logs to delete
}

}  // namespace basin::log_util
