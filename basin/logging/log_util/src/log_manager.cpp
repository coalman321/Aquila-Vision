#include "basin/log_util/log_manager.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>
#include <rosbag2_storage/storage_factory.hpp>
#include <rosbag2_storage/storage_options.hpp>

namespace basin::log_util {

namespace {

// MCAP layout (https://mcap.dev/spec): records are an opcode byte, a little-endian u64 length and the body. A
// finished file ends with a Footer record and the magic; the Footer points at the summary section, whose
// Statistics record holds the first and last message times
constexpr uint8_t MCAP_OP_FOOTER = 0x02;
constexpr uint8_t MCAP_OP_STATISTICS = 0x0B;
constexpr uint8_t MCAP_OP_SUMMARY_OFFSET = 0x0E;
constexpr size_t MCAP_MAGIC_SIZE = 8;
constexpr size_t MCAP_RECORD_HEADER_SIZE = 9;
constexpr size_t MCAP_FOOTER_BODY_SIZE = 20;
/// A summary bigger than this isn't worth reading for two numbers; rosbag2 can have it
constexpr uint64_t MCAP_MAX_SUMMARY_READ = 16 * 1024 * 1024;

uint64_t readLe(const char* p, size_t bytes) {
    uint64_t v = 0;
    for (size_t i = 0; i < bytes; i++) {
        v |= static_cast<uint64_t>(static_cast<uint8_t>(p[i])) << (8 * i);
    }
    return v;
}

bool preadAll(int fd, std::string& out, uint64_t offset, uint64_t length) {
    out.resize(length);
    size_t done = 0;
    while (done < length) {
        ssize_t n = ::pread(fd, out.data() + done, length - done, static_cast<off_t>(offset + done));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        done += static_cast<size_t>(n);
    }
    return true;
}

/// Finds the first record with `opcode` among the records in `bytes`, returning its body
bool findRecord(const std::string& bytes, uint8_t opcode, std::string& body) {
    for (size_t pos = 0; pos + MCAP_RECORD_HEADER_SIZE <= bytes.size();) {
        uint8_t op = static_cast<uint8_t>(bytes[pos]);
        uint64_t length = readLe(bytes.data() + pos + 1, 8);
        if (length > bytes.size() - pos - MCAP_RECORD_HEADER_SIZE) {
            return false;
        }
        if (op == opcode) {
            body = bytes.substr(pos + MCAP_RECORD_HEADER_SIZE, length);
            return true;
        }
        pos += MCAP_RECORD_HEADER_SIZE + length;
    }
    return false;
}

/// Reads a finished MCAP file's message time range (nanoseconds) from its summary Statistics record: a few small
/// reads, where opening it through rosbag2 costs several milliseconds. False for a file with no messages or no
/// usable summary (still being written, cut off by a crash), for the caller to try another way
bool readMcapStatistics(const std::string& path, int64_t& start_ns, int64_t& end_ns) {
    int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        return false;
    }
    struct Closer {
        int fd;
        ~Closer() { ::close(fd); }
    } closer{ fd };

    struct stat st {};
    constexpr uint64_t footer_size = MCAP_RECORD_HEADER_SIZE + MCAP_FOOTER_BODY_SIZE;
    if (::fstat(fd, &st) != 0 || static_cast<uint64_t>(st.st_size) < 2 * MCAP_MAGIC_SIZE + footer_size) {
        return false;
    }
    const uint64_t footer_start = static_cast<uint64_t>(st.st_size) - MCAP_MAGIC_SIZE - footer_size;

    std::string footer;
    if (!preadAll(fd, footer, footer_start, footer_size + MCAP_MAGIC_SIZE) ||
        footer.compare(footer_size, MCAP_MAGIC_SIZE, "\x89MCAP0\r\n", MCAP_MAGIC_SIZE) != 0 ||
        static_cast<uint8_t>(footer[0]) != MCAP_OP_FOOTER || readLe(footer.data() + 1, 8) != MCAP_FOOTER_BODY_SIZE) {
        return false;
    }
    const uint64_t summary_start = readLe(footer.data() + MCAP_RECORD_HEADER_SIZE, 8);
    const uint64_t summary_offset_start = readLe(footer.data() + MCAP_RECORD_HEADER_SIZE + 8, 8);
    if (summary_start == 0 || summary_start > footer_start ||
        (summary_offset_start != 0 && (summary_offset_start < summary_start || summary_offset_start > footer_start))) {
        return false;
    }

    // The summary offsets, when there are any, say where the Statistics record is; otherwise search the summary
    uint64_t search_start = summary_start;
    uint64_t search_end = summary_offset_start != 0 ? summary_offset_start : footer_start;
    if (summary_offset_start != 0) {
        std::string offsets;
        if (footer_start - summary_offset_start > MCAP_MAX_SUMMARY_READ ||
            !preadAll(fd, offsets, summary_offset_start, footer_start - summary_offset_start)) {
            return false;
        }
        for (size_t pos = 0; pos + MCAP_RECORD_HEADER_SIZE + 17 <= offsets.size();) {
            uint64_t length = readLe(offsets.data() + pos + 1, 8);
            const char* body = offsets.data() + pos + MCAP_RECORD_HEADER_SIZE;
            if (static_cast<uint8_t>(offsets[pos]) == MCAP_OP_SUMMARY_OFFSET && length >= 17 &&
                static_cast<uint8_t>(body[0]) == MCAP_OP_STATISTICS) {
                uint64_t group_start = readLe(body + 1, 8);
                uint64_t group_length = readLe(body + 9, 8);
                if (group_start >= summary_start && group_length <= search_end - summary_start &&
                    group_start - summary_start <= search_end - summary_start - group_length) {
                    search_start = group_start;
                    search_end = group_start + group_length;
                }
                break;
            }
            if (length > offsets.size() - pos - MCAP_RECORD_HEADER_SIZE) {
                break;
            }
            pos += MCAP_RECORD_HEADER_SIZE + length;
        }
    }

    std::string summary;
    std::string statistics;
    if (search_end - search_start > MCAP_MAX_SUMMARY_READ ||
        !preadAll(fd, summary, search_start, search_end - search_start) ||
        !findRecord(summary, MCAP_OP_STATISTICS, statistics) || statistics.size() < 42) {
        return false;
    }
    // message_count u64, schema_count u16, channel_count u32, attachment_count u32, metadata_count u32,
    // chunk_count u32, message_start_time u64, message_end_time u64, ...
    if (readLe(statistics.data(), 8) == 0) {
        return false;
    }
    start_ns = static_cast<int64_t>(readLe(statistics.data() + 26, 8));
    end_ns = static_cast<int64_t>(readLe(statistics.data() + 34, 8));
    return end_ns >= start_ns;
}

}  // namespace

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

    int64_t start_ns = 0;
    int64_t end_ns = 0;
    if (!readMcapStatistics(log_path, start_ns, end_ns)) {
        try {
            // Open the MCAP file using rosbag2's storage interface, which can also read one with no summary
            rosbag2_storage::StorageFactory factory;
            rosbag2_storage::StorageOptions options;
            options.uri = log_path;
            options.storage_id = "mcap";

            auto storage = factory.open_read_only(options);
            if (!storage) {
                return false;
            }

            auto metadata = storage->get_metadata();
            start_ns = metadata.starting_time.time_since_epoch().count();
            end_ns = start_ns + metadata.duration.count();
        } catch (const std::exception& e) {
            return false;
        }
    }

    // Convert ROS time (nanoseconds) to epoch seconds, widening to whole seconds so a file that covers part
    // of a snapshot's first or last second still counts as overlapping it
    start_time = start_ns / 1000000000LL;
    end_time = (end_ns + 999999999LL) / 1000000000LL;
    return true;
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
