#include "basin/log_util/snapshot_writer.hpp"

#include <fcntl.h>
#include <unistd.h>
#include <yaml-cpp/yaml.h>

#include <cstring>
#include <filesystem>
#include <fstream>

namespace basin::log_util {

SnapshotWriter::SnapshotWriter() {}

SnapshotWriter::~SnapshotWriter() {}

void SnapshotWriter::set_recording_directory(const std::string& directory) {
    std::lock_guard<std::mutex> lock(mutex_);
    recording_directory_ = directory;
}

bool SnapshotWriter::start_snapshot(const std::string& snapshot_id, const std::string& trigger,
                                    const std::string& description, const std::vector<std::string>& tags,
                                    int64_t start_time_override) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Check if snapshot already exists
    if (snapshots_.find(snapshot_id) != snapshots_.end()) {
        return false;
    }

    // Create new snapshot
    SnapshotInfo info;
    info.id = snapshot_id;

    if (start_time_override >= 0) {
        info.start_time = start_time_override;
    } else {
        info.start_time =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
    }

    info.end_time = -1;  // Not yet finished
    info.trigger = trigger;
    info.description = description;
    info.tags = tags;

    snapshots_[snapshot_id] = info;

    // Save to file
    return save_to_file();
}

bool SnapshotWriter::stop_snapshot(const std::string& snapshot_id, int64_t end_time_override) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
        return false;
    }

    // Check if already stopped
    if (it->second.end_time != -1) {
        return false;
    }

    // Update end time
    if (end_time_override >= 0) {
        it->second.end_time = end_time_override;
    } else {
        it->second.end_time =
            std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch())
                .count();
    }

    // Save to file
    return save_to_file();
}

bool SnapshotWriter::has_snapshot(const std::string& snapshot_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return snapshots_.find(snapshot_id) != snapshots_.end();
}

bool SnapshotWriter::is_snapshot_running(const std::string& snapshot_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
        return false;
    }

    return it->second.end_time == -1;
}

SnapshotInfo SnapshotWriter::get_snapshot(const std::string& snapshot_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = snapshots_.find(snapshot_id);
    if (it == snapshots_.end()) {
        return SnapshotInfo();
    }

    return it->second;
}

std::vector<SnapshotInfo> SnapshotWriter::get_all_snapshots() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<SnapshotInfo> result;
    result.reserve(snapshots_.size());

    for (const auto& pair : snapshots_) {
        result.push_back(pair.second);
    }

    return result;
}

bool SnapshotWriter::load_from_file() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (recording_directory_.empty()) {
        return false;
    }

    std::filesystem::path snapshot_path = std::filesystem::path(recording_directory_) / SNAPSHOT_FILENAME;
    std::filesystem::path tmp_path =
        std::filesystem::path(recording_directory_) / (std::string(SNAPSHOT_FILENAME) + ".tmp");

    // Recovery: if main file absent but tmp exists, a crash occurred during rename — complete it
    if (!std::filesystem::exists(snapshot_path) && std::filesystem::exists(tmp_path)) {
        std::filesystem::rename(tmp_path, snapshot_path);
    }

    if (!std::filesystem::exists(snapshot_path)) {
        // File doesn't exist yet, which is fine for first run
        return true;
    }

    try {
        YAML::Node yaml = YAML::LoadFile(snapshot_path.string());

        if (!yaml["version"] || yaml["version"].as<int>() != SNAPSHOT_FILE_VERSION) {
            return false;
        }

        snapshots_.clear();

        if (yaml["snapshots"]) {
            for (const auto& snapshot_node : yaml["snapshots"]) {
                SnapshotInfo info;
                info.id = snapshot_node["id"].as<std::string>();
                info.start_time = snapshot_node["start_time"].as<int64_t>();
                info.end_time = snapshot_node["end_time"].as<int64_t>();
                info.trigger = snapshot_node["trigger"].as<std::string>();
                info.description = snapshot_node["description"].as<std::string>("");

                if (snapshot_node["tags"]) {
                    for (const auto& tag : snapshot_node["tags"]) {
                        info.tags.push_back(tag.as<std::string>());
                    }
                }

                snapshots_[info.id] = info;
            }
        }

        return true;
    } catch (const YAML::Exception& e) {
        return false;
    }
}

bool SnapshotWriter::ensure_file_exists() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (recording_directory_.empty()) {
        return false;
    }

    std::filesystem::path snapshot_path = std::filesystem::path(recording_directory_) / SNAPSHOT_FILENAME;

    // If file already exists, nothing to do
    if (std::filesystem::exists(snapshot_path)) {
        return true;
    }

    // Create an empty snapshots file atomically
    std::filesystem::path tmp_path =
        std::filesystem::path(recording_directory_) / (std::string(SNAPSHOT_FILENAME) + ".tmp");

    try {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "version" << YAML::Value << SNAPSHOT_FILE_VERSION;
        out << YAML::Key << "snapshots" << YAML::Value << YAML::BeginSeq;
        out << YAML::EndSeq;
        out << YAML::EndMap;

        int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            return false;
        }
        const char* content = out.c_str();
        size_t len = std::strlen(content);
        ssize_t written = ::write(fd, content, len);
        ::fdatasync(fd);
        ::close(fd);

        if (written != static_cast<ssize_t>(len)) {
            return false;
        }

        std::filesystem::rename(tmp_path, snapshot_path);
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool SnapshotWriter::save_to_file() {
    // Note: This should be called with mutex already locked

    if (recording_directory_.empty()) {
        return false;
    }

    std::filesystem::path snapshot_path = std::filesystem::path(recording_directory_) / SNAPSHOT_FILENAME;
    std::filesystem::path tmp_path =
        std::filesystem::path(recording_directory_) / (std::string(SNAPSHOT_FILENAME) + ".tmp");

    try {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "version" << YAML::Value << SNAPSHOT_FILE_VERSION;
        out << YAML::Key << "snapshots" << YAML::Value << YAML::BeginSeq;

        for (const auto& pair : snapshots_) {
            const SnapshotInfo& info = pair.second;
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << info.id;
            out << YAML::Key << "start_time" << YAML::Value << info.start_time;
            out << YAML::Key << "end_time" << YAML::Value << info.end_time;
            out << YAML::Key << "trigger" << YAML::Value << info.trigger;
            out << YAML::Key << "description" << YAML::Value << info.description;
            out << YAML::Key << "tags" << YAML::Value << YAML::BeginSeq;
            for (const auto& tag : info.tags) {
                out << tag;
            }
            out << YAML::EndSeq;
            out << YAML::EndMap;
        }

        out << YAML::EndSeq;
        out << YAML::EndMap;

        int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            return false;
        }
        const char* content = out.c_str();
        size_t len = std::strlen(content);
        ssize_t written = ::write(fd, content, len);
        ::fdatasync(fd);
        ::close(fd);

        if (written != static_cast<ssize_t>(len)) {
            return false;
        }

        std::filesystem::rename(tmp_path, snapshot_path);
        return true;
    } catch (const std::exception& e) {
        return false;
    }
}

bool SnapshotWriter::save_wal(const WalState& state) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (recording_directory_.empty()) {
        return false;
    }

    std::filesystem::path wal_path = std::filesystem::path(recording_directory_) / WAL_FILENAME;
    std::filesystem::path tmp_path = std::filesystem::path(recording_directory_) / (std::string(WAL_FILENAME) + ".tmp");

    try {
        YAML::Emitter out;
        out << YAML::BeginMap;
        out << YAML::Key << "version" << YAML::Value << WAL_FILE_VERSION;

        out << YAML::Key << "open_snapshots" << YAML::Value << YAML::BeginSeq;
        for (const auto& s : state.open_snapshots) {
            out << YAML::BeginMap;
            out << YAML::Key << "id" << YAML::Value << s.id;
            out << YAML::Key << "start_time" << YAML::Value << s.start_time;
            out << YAML::Key << "trigger" << YAML::Value << s.trigger;
            out << YAML::Key << "description" << YAML::Value << s.description;
            out << YAML::Key << "tags" << YAML::Value << YAML::BeginSeq;
            for (const auto& tag : s.tags) {
                out << tag;
            }
            out << YAML::EndSeq;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        out << YAML::Key << "pending_event_snapshots" << YAML::Value << YAML::BeginSeq;
        for (const auto& p : state.pending_event_snapshots) {
            out << YAML::BeginMap;
            out << YAML::Key << "snapshot_id" << YAML::Value << p.snapshot_id;
            out << YAML::Key << "event_name" << YAML::Value << p.event_name;
            out << YAML::Key << "finalize_time_sec" << YAML::Value << p.finalize_time_sec;
            out << YAML::Key << "finalize_time_nsec" << YAML::Value << p.finalize_time_nsec;
            out << YAML::EndMap;
        }
        out << YAML::EndSeq;

        out << YAML::EndMap;

        int fd = ::open(tmp_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0) {
            return false;
        }
        const char* content = out.c_str();
        size_t len = std::strlen(content);
        ssize_t written = ::write(fd, content, len);
        ::fdatasync(fd);
        ::close(fd);

        if (written != static_cast<ssize_t>(len)) {
            return false;
        }

        std::filesystem::rename(tmp_path, wal_path);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

bool SnapshotWriter::load_wal(WalState& state) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (recording_directory_.empty()) {
        return false;
    }

    std::filesystem::path wal_path = std::filesystem::path(recording_directory_) / WAL_FILENAME;
    std::filesystem::path tmp_path = std::filesystem::path(recording_directory_) / (std::string(WAL_FILENAME) + ".tmp");

    // Recovery: if main absent but tmp exists, complete the rename
    if (!std::filesystem::exists(wal_path) && std::filesystem::exists(tmp_path)) {
        std::filesystem::rename(tmp_path, wal_path);
    }

    if (!std::filesystem::exists(wal_path)) {
        return false;
    }

    try {
        YAML::Node yaml = YAML::LoadFile(wal_path.string());

        if (!yaml["version"] || yaml["version"].as<int>() != WAL_FILE_VERSION) {
            return false;
        }

        state.open_snapshots.clear();
        state.pending_event_snapshots.clear();

        if (yaml["open_snapshots"]) {
            for (const auto& node : yaml["open_snapshots"]) {
                WalOpenSnapshot s;
                s.id = node["id"].as<std::string>();
                s.start_time = node["start_time"].as<int64_t>();
                s.trigger = node["trigger"].as<std::string>("");
                s.description = node["description"].as<std::string>("");
                if (node["tags"]) {
                    for (const auto& tag : node["tags"]) {
                        s.tags.push_back(tag.as<std::string>());
                    }
                }
                state.open_snapshots.push_back(s);
            }
        }

        if (yaml["pending_event_snapshots"]) {
            for (const auto& node : yaml["pending_event_snapshots"]) {
                WalPendingEventSnapshot p;
                p.snapshot_id = node["snapshot_id"].as<std::string>();
                p.event_name = node["event_name"].as<std::string>();
                p.finalize_time_sec = node["finalize_time_sec"].as<int64_t>();
                p.finalize_time_nsec = node["finalize_time_nsec"].as<int32_t>(0);
                state.pending_event_snapshots.push_back(p);
            }
        }

        return true;
    } catch (const YAML::Exception&) {
        return false;
    }
}

bool SnapshotWriter::clear_wal() {
    std::lock_guard<std::mutex> lock(mutex_);

    if (recording_directory_.empty()) {
        return false;
    }

    std::filesystem::path wal_path = std::filesystem::path(recording_directory_) / WAL_FILENAME;
    std::filesystem::path tmp_path = std::filesystem::path(recording_directory_) / (std::string(WAL_FILENAME) + ".tmp");

    try {
        if (std::filesystem::exists(wal_path)) {
            std::filesystem::remove(wal_path);
        }
        if (std::filesystem::exists(tmp_path)) {
            std::filesystem::remove(tmp_path);
        }
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace basin::log_util
