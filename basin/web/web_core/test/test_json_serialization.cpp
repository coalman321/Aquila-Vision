#include <gtest/gtest.h>

#include "basin/web_core/json_serialization.hpp"

using namespace basin::web_core::json_serialization;

// Test basic time serialization
TEST(JsonSerializationTest, TimeSerializationRoundTrip) {
    builtin_interfaces::msg::Time original;
    original.sec = 1234567890;
    original.nanosec = 987654321;

    std::string json_str = message_to_json_string(original);
    auto deserialized = json_string_to_message<builtin_interfaces::msg::Time>(json_str);

    EXPECT_EQ(deserialized.sec, original.sec);
    EXPECT_EQ(deserialized.nanosec, original.nanosec);
}

// Test header serialization with nested time
TEST(JsonSerializationTest, HeaderSerializationRoundTrip) {
    std_msgs::msg::Header original;
    original.stamp.sec = 1234567890;
    original.stamp.nanosec = 123456789;
    original.frame_id = "test_frame";

    std::string json_str = message_to_json_string(original);
    auto deserialized = json_string_to_message<std_msgs::msg::Header>(json_str);

    EXPECT_EQ(deserialized.stamp.sec, original.stamp.sec);
    EXPECT_EQ(deserialized.stamp.nanosec, original.stamp.nanosec);
    EXPECT_EQ(deserialized.frame_id, original.frame_id);
}

// Test ProcessInfo serialization
TEST(JsonSerializationTest, ProcessInfoSerializationRoundTrip) {
    process_msgs::msg::ProcessInfo original;
    original.process_id = 42;
    original.process_name = "test_process";
    original.process_status = 2;
    original.restart_counter = 5;
    original.local_pid = 12345;
    original.process_cpu_pct = 25.5f;
    original.process_memory = 102400;

    std::string json_str = message_to_json_string(original);
    auto deserialized = json_string_to_message<process_msgs::msg::ProcessInfo>(json_str);

    EXPECT_EQ(deserialized.process_id, original.process_id);
    EXPECT_EQ(deserialized.process_name, original.process_name);
    EXPECT_EQ(deserialized.process_status, original.process_status);
    EXPECT_EQ(deserialized.restart_counter, original.restart_counter);
    EXPECT_EQ(deserialized.local_pid, original.local_pid);
    EXPECT_FLOAT_EQ(deserialized.process_cpu_pct, original.process_cpu_pct);
    EXPECT_EQ(deserialized.process_memory, original.process_memory);
}

// Test ProcessHeartbeat with header
TEST(JsonSerializationTest, ProcessHeartbeatSerializationRoundTrip) {
    process_msgs::msg::ProcessHeartbeat original;
    original.header.stamp.sec = 1234567890;
    original.header.stamp.nanosec = 123456789;
    original.header.frame_id = "heartbeat_frame";
    original.process_name = "heartbeat_node";
    original.mode = 3;

    std::string json_str = message_to_json_string(original);
    auto deserialized = json_string_to_message<process_msgs::msg::ProcessHeartbeat>(json_str);

    EXPECT_EQ(deserialized.header.stamp.sec, original.header.stamp.sec);
    EXPECT_EQ(deserialized.header.stamp.nanosec, original.header.stamp.nanosec);
    EXPECT_EQ(deserialized.header.frame_id, original.header.frame_id);
    EXPECT_EQ(deserialized.process_name, original.process_name);
    EXPECT_EQ(deserialized.mode, original.mode);
}

// Test ProcessList with array of ProcessInfo
TEST(JsonSerializationTest, ProcessListSerializationRoundTrip) {
    process_msgs::msg::ProcessList original;
    original.header.stamp.sec = 1234567890;
    original.header.stamp.nanosec = 987654321;
    original.header.frame_id = "list_frame";
    original.system_name = "test_system";
    original.active_config = "production";

    process_msgs::msg::ProcessInfo proc1;
    proc1.process_id = 1;
    proc1.process_name = "node1";
    proc1.process_status = 2;
    proc1.local_pid = 100;

    process_msgs::msg::ProcessInfo proc2;
    proc2.process_id = 2;
    proc2.process_name = "node2";
    proc2.process_status = 2;
    proc2.local_pid = 200;

    original.processes.push_back(proc1);
    original.processes.push_back(proc2);

    std::string json_str = message_to_json_string(original);
    auto deserialized = json_string_to_message<process_msgs::msg::ProcessList>(json_str);

    EXPECT_EQ(deserialized.header.stamp.sec, original.header.stamp.sec);
    EXPECT_EQ(deserialized.system_name, original.system_name);
    EXPECT_EQ(deserialized.active_config, original.active_config);
    ASSERT_EQ(deserialized.processes.size(), 2);
    EXPECT_EQ(deserialized.processes[0].process_name, "node1");
    EXPECT_EQ(deserialized.processes[0].local_pid, 100);
    EXPECT_EQ(deserialized.processes[1].process_name, "node2");
    EXPECT_EQ(deserialized.processes[1].local_pid, 200);
}

// Test JSON formatting
TEST(JsonSerializationTest, JsonFormattingWithIndent) {
    process_msgs::msg::ProcessInfo original;
    original.process_id = 1;
    original.process_name = "test";

    std::string json_compact = message_to_json_string(original);
    std::string json_pretty = message_to_json_string(original, 2);

    // Pretty printed should be longer due to whitespace
    EXPECT_GT(json_pretty.size(), json_compact.size());
    
    // Both should deserialize to the same values
    auto from_compact = json_string_to_message<process_msgs::msg::ProcessInfo>(json_compact);
    auto from_pretty = json_string_to_message<process_msgs::msg::ProcessInfo>(json_pretty);
    
    EXPECT_EQ(from_compact.process_id, from_pretty.process_id);
    EXPECT_EQ(from_compact.process_name, from_pretty.process_name);
}

