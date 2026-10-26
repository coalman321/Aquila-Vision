#include <gtest/gtest.h>

#include <filesystem>
#include <rclcpp/rclcpp.hpp>
#include <stdexcept>
#include <string>
#include <vector>

#include "pcap_recorder/pcap_recorder_params.hpp"

using basin::pcap_recorder::faultMatches;

TEST(FaultMatches, Wildcards) {
    EXPECT_TRUE(faultMatches({ "*" }, "drive", "motor_overcurrent"));
    EXPECT_TRUE(faultMatches({ "drive/*" }, "drive", "motor_overcurrent"));
    EXPECT_TRUE(faultMatches({ "power/x", "drive/motor_overcurrent" }, "drive", "motor_overcurrent"));
    EXPECT_FALSE(faultMatches({ "drive/*" }, "drivetrain", "motor_overcurrent"));
    EXPECT_FALSE(faultMatches({ "drive/motor" }, "drive", "motor_overcurrent"));
    EXPECT_FALSE(faultMatches({}, "drive", "motor_overcurrent"));
}

TEST(BuildTcpdumpArgs, PutsExtraArgsAfterTheFixedOnesAndTheFilterLast) {
    rclcpp::init(0, nullptr);
    {
        rclcpp::NodeOptions options;
        options.append_parameter_override("tcpdump.path", std::string("/bin/sh"));
        options.append_parameter_override("tcpdump.interface", std::string("eth1"));
        options.append_parameter_override("tcpdump.snaplen", 128);
        options.append_parameter_override("tcpdump.immediate_mode", true);
        options.append_parameter_override("tcpdump.extra_args", std::vector<std::string>{ "-n", "-p" });
        options.append_parameter_override("tcpdump.filter", std::string("udp and port 2368"));
        auto node = std::make_shared<rclcpp::Node>("params_test", options);
        basin::pcap_recorder::PcapRecorderParams params(node.get());

        EXPECT_EQ(basin::pcap_recorder::buildTcpdumpArgs(params),
                  (std::vector<std::string>{ "-i", "eth1", "-s", "128", "-U", "-w", "-", "--immediate-mode", "-n", "-p",
                                             "udp and port 2368" }));
    }
    rclcpp::shutdown();
}

TEST(PcapRecorderParams, RejectsAMissingTcpdump) {
    rclcpp::init(0, nullptr);
    {
        rclcpp::NodeOptions options;
        options.append_parameter_override("tcpdump.path", std::string("/nonexistent/tcpdump"));
        auto node = std::make_shared<rclcpp::Node>("params_test", options);
        EXPECT_THROW(basin::pcap_recorder::PcapRecorderParams params(node.get()), std::runtime_error);
    }
    rclcpp::shutdown();
}

TEST(PcapRecorderParams, AnEmptyPathFindsTheBundledTcpdump) {
    rclcpp::init(0, nullptr);
    {
        auto node = std::make_shared<rclcpp::Node>("params_test");
        basin::pcap_recorder::PcapRecorderParams params(node.get());

        const std::filesystem::path path(params.tcpdump_path);
        EXPECT_TRUE(path.is_absolute()) << path;
        EXPECT_EQ(path.filename(), "tcpdump");
        EXPECT_TRUE(std::filesystem::exists(path)) << path;
    }
    rclcpp::shutdown();
}
