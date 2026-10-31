#include <gtest/gtest.h>
#include <unistd.h>

#include <filesystem>
#include <limits>
#include <rosbag2_storage/storage_factory.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <string>
#include <tf2_msgs/msg/tf_message.hpp>
#include <vector>

#include "basin/log_util/log_writer.hpp"

using basin::log_util::LogWriter;

namespace {

/// Writes real MCAP files into a scratch directory and reads back the schemas they carry.
class LogWriterTest : public ::testing::Test {
   protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("log_writer_test_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_);
    }

    void TearDown() override { std::filesystem::remove_all(dir_); }

    /// Every schema in the MCAP file at `path`
    static std::vector<rosbag2_storage::MessageDefinition> readDefinitions(const std::string& path) {
        rosbag2_storage::StorageFactory factory;
        rosbag2_storage::StorageOptions options;
        options.uri = path;
        options.storage_id = "mcap";
        auto storage = factory.open_read_only(options);
        std::vector<rosbag2_storage::MessageDefinition> definitions;
        storage->get_all_message_definitions(definitions);
        return definitions;
    }

    /// Writes one sensor_msgs/Image to `topic`
    static void writeImage(LogWriter& writer, const std::string& topic, int64_t stamp_ns) {
        sensor_msgs::msg::Image image;
        auto serialized = std::make_shared<rclcpp::SerializedMessage>();
        rclcpp::Serialization<sensor_msgs::msg::Image>().serialize_message(&image, serialized.get());
        writer.write(serialized, topic, "sensor_msgs/msg/Image", rclcpp::Time(stamp_ns));
    }

    /// The definition of `type` among `definitions`, failing the test if there is none
    static rosbag2_storage::MessageDefinition find(const std::vector<rosbag2_storage::MessageDefinition>& definitions,
                                                   const std::string& type) {
        for (const auto& definition : definitions) {
            if (definition.topic_type == type) {
                return definition;
            }
        }
        ADD_FAILURE() << "no schema for " << type;
        return {};
    }

    std::filesystem::path dir_;
};

// Foxglove can only decode a channel whose schema is the type's full `.msg` text, encoded "ros2msg".
TEST_F(LogWriterTest, SchemasCarryFullMessageDefinitions) {
    LogWriter writer;
    writer.open(basin::log_util::make_mcap_storage_options(dir_.string(), 3600, std::numeric_limits<long>::max()),
                rosbag2_cpp::ConverterOptions(), "log");
    writeImage(writer, "/camera/debug_image", 1'000'000'000);
    tf2_msgs::msg::TFMessage tf;
    auto serialized_tf = std::make_shared<rclcpp::SerializedMessage>();
    rclcpp::Serialization<tf2_msgs::msg::TFMessage>().serialize_message(&tf, serialized_tf.get());
    writer.write(serialized_tf, "/tf_static", "tf2_msgs/msg/TFMessage", rclcpp::Time(1'000'000'000));
    std::string path = writer.get_current_file_path();
    writer.close();

    auto definitions = readDefinitions(path);
    for (const std::string type :
         { "sensor_msgs/msg/Image", "tf2_msgs/msg/TFMessage", "logging_msgs/msg/LogMetadata" }) {
        auto definition = find(definitions, type);
        EXPECT_EQ(definition.encoding, "ros2msg") << type;
        EXPECT_FALSE(definition.encoded_message_definition.empty()) << type;
    }
    // Image's Header is a dependency, so its definition (and builtin_interfaces/Time's) is inlined
    EXPECT_NE(find(definitions, "sensor_msgs/msg/Image").encoded_message_definition.find("MSG: std_msgs/Header"),
              std::string::npos);
}

// A split recreates every topic in the new file, which needs the same schemas.
TEST_F(LogWriterTest, SplitFilesKeepFullMessageDefinitions) {
    LogWriter writer;
    writer.open(basin::log_util::make_mcap_storage_options(dir_.string(), 3600, std::numeric_limits<long>::max()),
                rosbag2_cpp::ConverterOptions(), "log");
    writeImage(writer, "/camera/debug_image", 1'000'000'000);
    writer.force_split();
    writeImage(writer, "/camera/debug_image", 2'000'000'000);
    std::string path = writer.get_current_file_path();
    writer.close();

    auto definition = find(readDefinitions(path), "sensor_msgs/msg/Image");
    EXPECT_EQ(definition.encoding, "ros2msg");
    EXPECT_FALSE(definition.encoded_message_definition.empty());
}

}  // namespace
