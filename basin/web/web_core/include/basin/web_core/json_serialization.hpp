#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <vector>

// ROS2 message includes
#include <builtin_interfaces/msg/time.hpp>
#include <std_msgs/msg/header.hpp>

// Basin interface messages
#include <logging_msgs/msg/log_metadata.hpp>
#include <logging_msgs/srv/snapshot_request.hpp>
#include <process_msgs/msg/process_heartbeat.hpp>
#include <process_msgs/msg/process_info.hpp>
#include <process_msgs/msg/process_list.hpp>
#include <process_msgs/msg/system_info.hpp>
#include <process_msgs/srv/remote_request.hpp>
#include <status_msgs/msg/fault_key.hpp>
#include <status_msgs/msg/fault_report.hpp>
#include <status_msgs/msg/killswitch_report.hpp>
#include <status_msgs/msg/safety_status.hpp>
#include <timesync_msgs/msg/sync_status.hpp>

namespace basin::web_core {
namespace json_serialization {

using json = nlohmann::json;

// =============================================================================
// Standard ROS2 Messages
// =============================================================================

/// @brief Serialize builtin_interfaces/Time to JSON
void to_json(json& j, const builtin_interfaces::msg::Time& msg);

/// @brief Deserialize builtin_interfaces/Time from JSON
void from_json(const json& j, builtin_interfaces::msg::Time& msg);

/// @brief Serialize std_msgs/Header to JSON
void to_json(json& j, const std_msgs::msg::Header& msg);

/// @brief Deserialize std_msgs/Header from JSON
void from_json(const json& j, std_msgs::msg::Header& msg);

// =============================================================================
// Process Messages
// =============================================================================

/// @brief Serialize ProcessInfo to JSON
void to_json(json& j, const process_msgs::msg::ProcessInfo& msg);

/// @brief Deserialize ProcessInfo from JSON
void from_json(const json& j, process_msgs::msg::ProcessInfo& msg);

/// @brief Serialize ProcessList to JSON
void to_json(json& j, const process_msgs::msg::ProcessList& msg);

/// @brief Deserialize ProcessList from JSON
void from_json(const json& j, process_msgs::msg::ProcessList& msg);

/// @brief Serialize ProcessHeartbeat to JSON
void to_json(json& j, const process_msgs::msg::ProcessHeartbeat& msg);

/// @brief Deserialize ProcessHeartbeat from JSON
void from_json(const json& j, process_msgs::msg::ProcessHeartbeat& msg);

/// @brief Serialize SystemInfo to JSON
void to_json(json& j, const process_msgs::msg::SystemInfo& msg);

/// @brief Deserialize SystemInfo from JSON
void from_json(const json& j, process_msgs::msg::SystemInfo& msg);

/// @brief Serialize RemoteRequest (Service) to JSON
void to_json(json& j, const process_msgs::srv::RemoteRequest::Request& msg);

/// @brief Deserialize RemoteRequest (Service) from JSON
void from_json(const json& j, process_msgs::srv::RemoteRequest::Request& msg);

/// @brief Serialize RemoteRequest Response to JSON
void to_json(json& j, const process_msgs::srv::RemoteRequest::Response& msg);

/// @brief Deserialize RemoteRequest Response from JSON
void from_json(const json& j, process_msgs::srv::RemoteRequest::Response& msg);

// =============================================================================
// Status Messages
// =============================================================================

/// @brief Serialize FaultKey to JSON
void to_json(json& j, const status_msgs::msg::FaultKey& msg);

/// @brief Deserialize FaultKey from JSON
void from_json(const json& j, status_msgs::msg::FaultKey& msg);

/// @brief Serialize FaultReport to JSON
void to_json(json& j, const status_msgs::msg::FaultReport& msg);

/// @brief Deserialize FaultReport from JSON
void from_json(const json& j, status_msgs::msg::FaultReport& msg);

/// @brief Serialize KillswitchReport to JSON
void to_json(json& j, const status_msgs::msg::KillswitchReport& msg);

/// @brief Deserialize KillswitchReport from JSON
void from_json(const json& j, status_msgs::msg::KillswitchReport& msg);

/// @brief Serialize SafetyStatus to JSON
void to_json(json& j, const status_msgs::msg::SafetyStatus& msg);

/// @brief Deserialize SafetyStatus from JSON
void from_json(const json& j, status_msgs::msg::SafetyStatus& msg);

// =============================================================================
// Logging Messages
// =============================================================================

/// @brief Serialize LogMetadata to JSON
void to_json(json& j, const logging_msgs::msg::LogMetadata& msg);

/// @brief Deserialize LogMetadata from JSON
void from_json(const json& j, logging_msgs::msg::LogMetadata& msg);

/// @brief Serialize SnapshotRequest (Service) to JSON
void to_json(json& j, const logging_msgs::srv::SnapshotRequest::Request& msg);

/// @brief Deserialize SnapshotRequest (Service) from JSON
void from_json(const json& j, logging_msgs::srv::SnapshotRequest::Request& msg);

/// @brief Serialize SnapshotRequest Response to JSON
void to_json(json& j, const logging_msgs::srv::SnapshotRequest::Response& msg);

/// @brief Deserialize SnapshotRequest Response from JSON
void from_json(const json& j, logging_msgs::srv::SnapshotRequest::Response& msg);

// =============================================================================
// Timesync Messages
// =============================================================================

/// @brief Serialize SyncStatus to JSON
void to_json(json& j, const timesync_msgs::msg::SyncStatus& msg);

/// @brief Deserialize SyncStatus from JSON
void from_json(const json& j, timesync_msgs::msg::SyncStatus& msg);

// =============================================================================
// Utility Functions
// =============================================================================

/// @brief Convert any supported ROS message to JSON string
/// @tparam T ROS message type
/// @param msg ROS message instance
/// @param indent Number of spaces for indentation (-1 for compact)
/// @return JSON string representation
template<typename T>
std::string message_to_json_string(const T& msg, int indent = -1) {
    json j;
    to_json(j, msg);
    return j.dump(indent);
}

/// @brief Convert JSON string to a ROS message
/// @tparam T ROS message type
/// @param json_str JSON string
/// @return ROS message instance
template<typename T>
T json_string_to_message(const std::string& json_str) {
    json j = json::parse(json_str);
    T msg;
    from_json(j, msg);
    return msg;
}

}  // namespace json_serialization
}  // namespace basin::web_core
