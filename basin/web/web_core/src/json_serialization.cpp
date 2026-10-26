#include "basin/web_core/json_serialization.hpp"

namespace basin {
namespace web_core {
namespace json_serialization {

// =============================================================================
// Standard ROS2 Messages
// =============================================================================

void to_json(json& j, const builtin_interfaces::msg::Time& msg) {
    j["sec"] = msg.sec;
    j["nanosec"] = msg.nanosec;
}

void from_json(const json& j, builtin_interfaces::msg::Time& msg) {
    msg.sec = j.at("sec").get<int32_t>();
    msg.nanosec = j.at("nanosec").get<uint32_t>();
}

void to_json(json& j, const std_msgs::msg::Header& msg) {
    to_json(j["stamp"], msg.stamp);
    j["frame_id"] = msg.frame_id;
}

void from_json(const json& j, std_msgs::msg::Header& msg) {
    from_json(j.at("stamp"), msg.stamp);
    msg.frame_id = j.at("frame_id").get<std::string>();
}

// =============================================================================
// Process Messages
// =============================================================================

void to_json(json& j, const process_msgs::msg::ProcessInfo& msg) {
    j["process_id"] = msg.process_id;
    j["process_name"] = msg.process_name;
    j["process_status"] = msg.process_status;
    j["restart_counter"] = msg.restart_counter;
    j["local_pid"] = msg.local_pid;
    j["process_cpu_pct"] = msg.process_cpu_pct;
    j["process_memory"] = msg.process_memory;
}

void from_json(const json& j, process_msgs::msg::ProcessInfo& msg) {
    msg.process_id = j.at("process_id").get<uint32_t>();
    msg.process_name = j.at("process_name").get<std::string>();
    msg.process_status = j.at("process_status").get<uint8_t>();
    msg.restart_counter = j.at("restart_counter").get<uint32_t>();
    msg.local_pid = j.at("local_pid").get<int32_t>();
    msg.process_cpu_pct = j.at("process_cpu_pct").get<float>();
    msg.process_memory = j.at("process_memory").get<uint64_t>();
}

void to_json(json& j, const process_msgs::msg::ProcessList& msg) {
    to_json(j["header"], msg.header);
    j["system_name"] = msg.system_name;
    j["active_config"] = msg.active_config;
    j["processes"] = json::array();
    for (const auto& proc : msg.processes) {
        json proc_json;
        to_json(proc_json, proc);
        j["processes"].push_back(proc_json);
    }
}

void from_json(const json& j, process_msgs::msg::ProcessList& msg) {
    from_json(j.at("header"), msg.header);
    msg.system_name = j.at("system_name").get<std::string>();
    msg.active_config = j.at("active_config").get<std::string>();
    const auto& processes_array = j.at("processes");
    msg.processes.clear();
    msg.processes.reserve(processes_array.size());
    for (const auto& proc_json : processes_array) {
        process_msgs::msg::ProcessInfo proc;
        from_json(proc_json, proc);
        msg.processes.push_back(proc);
    }
}

void to_json(json& j, const process_msgs::msg::ProcessHeartbeat& msg) {
    to_json(j["header"], msg.header);
    j["process_name"] = msg.process_name;
    j["mode"] = msg.mode;
}

void from_json(const json& j, process_msgs::msg::ProcessHeartbeat& msg) {
    from_json(j.at("header"), msg.header);
    msg.process_name = j.at("process_name").get<std::string>();
    msg.mode = j.at("mode").get<uint8_t>();
}

void to_json(json& j, const process_msgs::msg::SystemInfo& msg) {
    to_json(j["header"], msg.header);
    j["system_name"] = msg.system_name;
    j["per_core_cpu_user"] = msg.per_core_cpu_user;
    j["per_core_cpu_sys"] = msg.per_core_cpu_sys;
    j["used_memory"] = msg.used_memory;
    j["free_memory"] = msg.free_memory;
}

void from_json(const json& j, process_msgs::msg::SystemInfo& msg) {
    from_json(j.at("header"), msg.header);
    msg.system_name = j.at("system_name").get<std::string>();
    msg.per_core_cpu_user = j.at("per_core_cpu_user").get<std::vector<float>>();
    msg.per_core_cpu_sys = j.at("per_core_cpu_sys").get<std::vector<float>>();
    msg.used_memory = j.at("used_memory").get<uint64_t>();
    msg.free_memory = j.at("free_memory").get<uint64_t>();
}

void to_json(json& j, const process_msgs::srv::RemoteRequest::Request& msg) {
    j["request_id"] = msg.request_id;
    j["id_argument"] = msg.id_argument;
    j["scenario_argument"] = msg.scenario_argument;
}

void from_json(const json& j, process_msgs::srv::RemoteRequest::Request& msg) {
    msg.request_id = j.at("request_id").get<uint8_t>();
    msg.id_argument = j.at("id_argument").get<uint64_t>();
    msg.scenario_argument = j.at("scenario_argument").get<std::string>();
}

void to_json(json& j, const process_msgs::srv::RemoteRequest::Response& msg) {
    j["resp_code"] = msg.resp_code;
    j["response_msg"] = msg.response_msg;
}

void from_json(const json& j, process_msgs::srv::RemoteRequest::Response& msg) {
    msg.resp_code = j.at("resp_code").get<uint8_t>();
    msg.response_msg = j.at("response_msg").get<std::string>();
}

// =============================================================================
// Status Messages
// =============================================================================

void to_json(json& j, const status_msgs::msg::FaultKey& msg) {
    j["fault_system"] = msg.fault_system;
    j["fault_name"] = msg.fault_name;
}

void from_json(const json& j, status_msgs::msg::FaultKey& msg) {
    msg.fault_system = j.at("fault_system").get<std::string>();
    msg.fault_name = j.at("fault_name").get<std::string>();
}

void to_json(json& j, const status_msgs::msg::FaultReport& msg) {
    to_json(j["key"], msg.key);
    to_json(j["raise_time"], msg.raise_time);
    to_json(j["expiration_time"], msg.expiration_time);
    j["fault_desc"] = msg.fault_desc;
}

void from_json(const json& j, status_msgs::msg::FaultReport& msg) {
    from_json(j.at("key"), msg.key);
    from_json(j.at("raise_time"), msg.raise_time);
    from_json(j.at("expiration_time"), msg.expiration_time);
    msg.fault_desc = j.at("fault_desc").get<std::string>();
}

void to_json(json& j, const status_msgs::msg::KillswitchReport& msg) {
    j["sender_id"] = msg.sender_id;
    j["kill_switch_type"] = msg.kill_switch_type;
    j["switch_asserting_kill"] = msg.switch_asserting_kill;
    j["needs_heartbeat"] = msg.needs_heartbeat;
}

void from_json(const json& j, status_msgs::msg::KillswitchReport& msg) {
    msg.sender_id = j.at("sender_id").get<std::string>();
    msg.kill_switch_type = j.at("kill_switch_type").get<uint8_t>();
    msg.switch_asserting_kill = j.at("switch_asserting_kill").get<bool>();
    msg.needs_heartbeat = j.at("needs_heartbeat").get<bool>();
}

void to_json(json& j, const status_msgs::msg::SafetyStatus& msg) {
    to_json(j["stamp"], msg.stamp);
    j["stop_severity"] = msg.stop_severity;
    j["is_killed"] = msg.is_killed;
    j["asserting_switches"] = msg.asserting_switches;
    j["registered_switches"] = msg.registered_switches;
    j["is_faulted"] = msg.is_faulted;
    j["active_faults"] = json::array();
    for (const auto& fault : msg.active_faults) {
        json fault_json;
        to_json(fault_json, fault);
        j["active_faults"].push_back(fault_json);
    }
}

void from_json(const json& j, status_msgs::msg::SafetyStatus& msg) {
    from_json(j.at("stamp"), msg.stamp);
    msg.stop_severity = j.at("stop_severity").get<uint8_t>();
    msg.is_killed = j.at("is_killed").get<bool>();
    msg.asserting_switches = j.at("asserting_switches").get<std::vector<uint32_t>>();
    msg.registered_switches = j.at("registered_switches").get<std::vector<std::string>>();
    msg.is_faulted = j.at("is_faulted").get<bool>();
    msg.active_faults.clear();
    for (const auto& fault_json : j.at("active_faults")) {
        status_msgs::msg::FaultReport fault;
        from_json(fault_json, fault);
        msg.active_faults.push_back(fault);
    }
}

// =============================================================================
// Logging Messages
// =============================================================================

void to_json(json& j, const logging_msgs::msg::LogMetadata& msg) {
    to_json(j["header"], msg.header);
    j["timed_event"] = msg.timed_event;
    j["key_name"] = msg.key_name;
    j["yaml_metadata"] = msg.yaml_metadata;
}

void from_json(const json& j, logging_msgs::msg::LogMetadata& msg) {
    from_json(j.at("header"), msg.header);
    msg.timed_event = j.at("timed_event").get<bool>();
    msg.key_name = j.at("key_name").get<std::string>();
    msg.yaml_metadata = j.at("yaml_metadata").get<std::string>();
}

void to_json(json& j, const logging_msgs::srv::SnapshotRequest::Request& msg) {
    j["request_id"] = msg.request_id;
    j["snapshot_name"] = msg.snapshot_name;
    j["description"] = msg.description;
    j["tags"] = msg.tags;
}

void from_json(const json& j, logging_msgs::srv::SnapshotRequest::Request& msg) {
    msg.request_id = j.at("request_id").get<uint8_t>();
    msg.snapshot_name = j.at("snapshot_name").get<std::string>();
    msg.description = j.at("description").get<std::string>();
    msg.tags = j.at("tags").get<std::vector<std::string>>();
}

void to_json(json& j, const logging_msgs::srv::SnapshotRequest::Response& msg) {
    j["resp_code"] = msg.resp_code;
    j["response_msg"] = msg.response_msg;
}

void from_json(const json& j, logging_msgs::srv::SnapshotRequest::Response& msg) {
    msg.resp_code = j.at("resp_code").get<uint8_t>();
    msg.response_msg = j.at("response_msg").get<std::string>();
}

// =============================================================================
// Timesync Messages
// =============================================================================

void to_json(json& j, const timesync_msgs::msg::SyncStatus& msg) {
    j["hostname"] = msg.hostname;
    to_json(j["stamp"], msg.stamp);
    j["status"] = msg.status;
}

void from_json(const json& j, timesync_msgs::msg::SyncStatus& msg) {
    msg.hostname = j.at("hostname").get<std::string>();
    from_json(j.at("stamp"), msg.stamp);
    msg.status = j.at("status").get<uint8_t>();
}

}  // namespace json_serialization
}  // namespace web_core
}  // namespace basin
