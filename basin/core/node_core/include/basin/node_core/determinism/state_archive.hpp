#pragma once

#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace basin::node_core::determinism {

/// @brief Thin serialization wrapper used by `NodeCore::saveState`/`restoreState`.
///
/// Backed by `nlohmann::json` to match the serialization convention already used
/// elsewhere in this codebase (see `basin::web_core::json_serialization`), rather than
/// introducing a second library (e.g. cereal) for the same job.
class StateArchive {
   public:
    /// @brief Store a field under `key`. Any type with an nlohmann `to_json` overload
    /// (including all arithmetic/string/container types, and ROS messages that provide one) works.
    template <typename T>
    void put(const std::string& key, const T& value) {
        data_[key] = value;
    }

    /// @brief Read a field previously stored under `key`.
    /// @throws nlohmann::json::out_of_range if `key` is not present.
    template <typename T>
    T get(const std::string& key) const {
        return data_.at(key).get<T>();
    }

    /// @brief Read a field previously stored under `key`, or `default_value` if absent.
    template <typename T>
    T getOr(const std::string& key, const T& default_value) const {
        auto it = data_.find(key);
        if (it == data_.end()) {
            return default_value;
        }
        return it->get<T>();
    }

    bool has(const std::string& key) const { return data_.contains(key); }

    /// @brief Serialize the archive to a compact JSON string.
    std::string dump() const { return data_.dump(); }

    /// @brief Load the archive from a JSON string previously produced by `dump()`.
    void load(const std::string& serialized) { data_ = nlohmann::json::parse(serialized); }

    /// @brief Write the archive to `path` as JSON.
    void writeToFile(const std::string& path) const {
        std::ofstream out(path);
        out << data_.dump();
    }

    /// @brief Load the archive from a JSON file previously produced by `writeToFile()`.
    void readFromFile(const std::string& path) {
        std::ifstream in(path);
        data_ = nlohmann::json::parse(in);
    }

   private:
    nlohmann::json data_ = nlohmann::json::object();
};

}  // namespace basin::node_core::determinism
