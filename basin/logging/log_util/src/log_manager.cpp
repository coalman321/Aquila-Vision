#include "basin/log_util/log_manager.hpp"

#include <sys/statvfs.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <limits>
#include <rosbag2_storage/storage_factory.hpp>
#include <rosbag2_storage/storage_options.hpp>

namespace basin::log_util {

LogManager::LogManager() : rotation_threshold_percent_(80.0), snapshot_usage_threshold_percent_(50.0) {}

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

void LogManager::set_disk_limits(double rotation_threshold_percent, double snapshot_usage_threshold_percent) {
    std::lock_guard<std::mutex> lock(mutex_);
    rotation_threshold_percent_ = rotation_threshold_percent;
    snapshot_usage_threshold_percent_ = snapshot_usage_threshold_percent;
}

void LogManager::prepare_log_directory() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!std::filesystem::exists(log_directory_)) {
        std::filesystem::create_directories(log_directory_);
    }

    if (!std::filesystem::is_directory(log_directory_)) {
        throw std::runtime_error("Log path is not a directory: " + log_directory_);
    }
}

std::string LogManager::next_log_name(const std::string& prefix) const {
    std::lock_guard<std::mutex> lock(mutex_);

    // Take the highest run number seen rather than the newest file's, so a touched or restored older log
    // can't make a run reuse a number
    size_t next_run = 0;
    if (std::filesystem::is_directory(log_directory_)) {
        for (const auto& entry : std::filesystem::directory_iterator(log_directory_)) {
            if (!entry.is_regular_file() || entry.path().extension() != ".mcap") {
                continue;
            }
            // Skip empty files (likely from crashed previous runs)
            std::error_code error;
            if (std::filesystem::file_size(entry.path(), error) == 0 || error) {
                continue;
            }

            // The run number is the digits straight after the prefix; rosbag2 appends `_<split>` after it
            std::string name = entry.path().filename().string();
            if (name.compare(0, prefix.size(), prefix) != 0) {
                continue;
            }
            size_t digits_end = prefix.size();
            while (digits_end < name.size() && std::isdigit(static_cast<unsigned char>(name[digits_end]))) {
                digits_end++;
            }
            if (digits_end > prefix.size()) {
                size_t run = std::stoull(name.substr(prefix.size(), digits_end - prefix.size()));
                next_run = std::max(next_run, run + 1);
            }
        }
    }

    return prefix + std::to_string(next_run);
}

size_t LogManager::scan_log_directory() {
    std::lock_guard<std::mutex> lock(mutex_);
    return scan_log_directory_locked();
}

size_t LogManager::scan_log_directory_locked() {
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

DiskCheckResult LogManager::check_disk(const std::string& active_log_file) {
    std::lock_guard<std::mutex> lock(mutex_);

    scan_log_directory_locked();

    // Delete the oldest unprotected logs until usage is back under the threshold, or nothing deletable is left
    DiskCheckResult result;
    while (get_disk_usage_percent() >= rotation_threshold_percent_ &&
           delete_oldest_unprotected_log(active_log_file)) {
        result.rotated_files++;
    }

    result.disk_usage_percent = get_disk_usage_percent();
    result.rotation_blocked = result.disk_usage_percent >= rotation_threshold_percent_;

    for (const auto& log_file : log_files_) {
        result.log_bytes += log_file.size_bytes;
        if (log_file.associated_with_snapshot) {
            result.protected_bytes += log_file.size_bytes;
        }
    }
    uintmax_t capacity = get_disk_capacity_bytes();
    if (capacity > 0) {
        result.snapshot_usage_percent =
            (static_cast<double>(result.protected_bytes) / static_cast<double>(capacity)) * 100.0;
    }
    result.snapshot_usage_high = result.snapshot_usage_percent >= snapshot_usage_threshold_percent_;

    last_disk_check_ = result;
    return result;
}

DiskCheckResult LogManager::get_last_disk_check() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_disk_check_;
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

uintmax_t LogManager::get_disk_capacity_bytes() const {
    // Note: This should be called with mutex already locked or from const methods

    struct statvfs stat;
    if (log_directory_.empty() || statvfs(log_directory_.c_str(), &stat) != 0) {
        return 0;
    }
    return static_cast<uintmax_t>(stat.f_blocks) * stat.f_frsize;
}

const SnapshotWriter& LogManager::get_snapshot_writer() const { return snapshot_writer_; }

bool LogManager::is_log_file_associated_with_snapshot(const LogFileInfo& log_file) const {
    // Note: This should be called with mutex already locked

    // Check if log overlaps with any snapshot. One still running (end_time == -1) protects everything from its
    // start on, so an event snapshot's time_before window survives until the snapshot is finalized
    for (const auto& snapshot : snapshot_writer_.get_all_snapshots()) {
        int64_t snapshot_end = snapshot.end_time == -1 ? std::numeric_limits<int64_t>::max() : snapshot.end_time;

        // Check for time range overlap
        if (!(log_file.end_time < snapshot.start_time || log_file.start_time > snapshot_end)) {
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

        // Convert ROS time (nanoseconds) to epoch seconds, widening to whole seconds so a file that covers part
        // of a snapshot's first or last second still counts as overlapping it
        int64_t start_ns = metadata.starting_time.time_since_epoch().count();
        int64_t end_ns = start_ns + metadata.duration.count();
        start_time = start_ns / 1000000000LL;
        end_time = (end_ns + 999999999LL) / 1000000000LL;

        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool LogManager::delete_oldest_unprotected_log(const std::string& active_log_file) {
    // Note: This should be called with mutex already locked

    std::filesystem::path active_path = std::filesystem::path(active_log_file).lexically_normal();

    // Find oldest log file not associated with snapshot, never the one being written
    for (auto it = log_files_.begin(); it != log_files_.end(); ++it) {
        if (!active_log_file.empty() && std::filesystem::path(it->path).lexically_normal() == active_path) {
            continue;
        }
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
