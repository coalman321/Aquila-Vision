#include <arpa/inet.h>
#include <google/protobuf/text_format.h>
#include <google/protobuf/util/message_differencer.h>
#include <gtest/gtest.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "chassis_proxy_client/udp_server.hpp"
#include "rules_cc/cc/runfiles/runfiles.h"

namespace proxy_client {
namespace {

using google::protobuf::util::MessageDifferencer;
using rules_cc::cc::runfiles::Runfiles;
using ReceiveResult = UdpServer::ReceiveResult;

constexpr int kPortA = 47811;
constexpr int kPortB = 47812;

/// Polls the server until a packet arrives (the socket is non-blocking) or a second passes
ReceiveResult receive(UdpServer& server) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(1);
    while (std::chrono::steady_clock::now() < deadline) {
        const ReceiveResult result = server.processPackets();
        if (result != ReceiveResult::kNoPacket) {
            return result;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return ReceiveResult::kNoPacket;
}

/// Sends raw bytes to a port on localhost, bypassing UdpServer's framing
void sendRaw(const std::string& bytes, int port) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    ASSERT_GE(fd, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    ASSERT_EQ(sendto(fd, bytes.data(), bytes.size(), 0, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)),
              static_cast<ssize_t>(bytes.size()));
    close(fd);
}

class UdpServerTest : public ::testing::Test {
   protected:
    void SetUp() override {
        robot_ = std::make_unique<UdpServer>(kPortA, "127.0.0.1", kPortB);
        coproc_ = std::make_unique<UdpServer>(kPortB, "127.0.0.1", kPortA);
    }

    std::unique_ptr<UdpServer> robot_;
    std::unique_ptr<UdpServer> coproc_;
};

TEST_F(UdpServerTest, NoPacketWhenIdle) { EXPECT_EQ(coproc_->processPackets(), ReceiveResult::kNoPacket); }

TEST_F(UdpServerTest, DispatchesToTheHandlerForThePayload) {
    std::optional<v1::MatchInfo> match_info;
    bool snapshot_called = false;
    coproc_->registerHandler(v1::Envelope::kMatchInfo,
                             [&](const v1::Envelope& envelope) { match_info = envelope.match_info(); });
    coproc_->registerHandler(v1::Envelope::kAutoSnapshot, [&](const v1::Envelope&) { snapshot_called = true; });

    v1::Envelope envelope;
    v1::MatchInfo* sent = envelope.mutable_match_info();
    sent->set_match_num(42);
    sent->set_match_type(v1::MATCH_TYPE_QUALIFICATION);
    sent->set_station(v1::ALLIANCE_STATION_RED_2);
    sent->set_event_name("MOKC Kansas City");
    robot_->send(envelope);

    ASSERT_EQ(receive(*coproc_), ReceiveResult::kDispatched);
    ASSERT_TRUE(match_info.has_value());
    EXPECT_TRUE(MessageDifferencer::Equals(*match_info, *sent));
    EXPECT_FALSE(snapshot_called);
}

TEST_F(UdpServerTest, ReportsPayloadsWithoutAHandler) {
    v1::Envelope envelope;
    envelope.mutable_auto_snapshot()->set_event_name("auto_start");
    robot_->send(envelope);

    EXPECT_EQ(receive(*coproc_), ReceiveResult::kUnhandled);
}

TEST_F(UdpServerTest, DropsOtherProtocolVersions) {
    bool called = false;
    coproc_->registerHandler(v1::Envelope::kAutoSnapshot, [&](const v1::Envelope&) { called = true; });

    v1::Envelope envelope;
    envelope.set_protocol_version(v1::PROTOCOL_VERSION_CURRENT + 1);
    envelope.mutable_auto_snapshot()->set_event_name("auto_start");
    sendRaw(envelope.SerializeAsString(), kPortB);

    EXPECT_EQ(receive(*coproc_), ReceiveResult::kVersionMismatch);
    EXPECT_FALSE(called);
}

TEST_F(UdpServerTest, DropsMalformedPackets) {
    // The legacy hand-rolled MatchInfo framing: msg id 50, then raw bytes
    sendRaw(std::string("\x32\x2a\x05\x02\x00\x04MOKC", 10), kPortB);

    EXPECT_EQ(receive(*coproc_), ReceiveResult::kMalformed);
}

/// Each golden frame from MW-Lib must decode to the message in its .txtpb, both as protoc encoded it (which
/// is also what we send) and as MW-Lib's QuickBuffers classes encode it (what the roboRIO sends; it orders
/// fields differently). MW-Lib's Java tests check the same files.
class GoldenFrameTest : public ::testing::TestWithParam<std::string> {
   protected:
    static std::string readFile(const std::string& rlocation) {
        std::string error;
        std::unique_ptr<Runfiles> runfiles(Runfiles::CreateForTest(BAZEL_CURRENT_REPOSITORY, &error));
        EXPECT_NE(runfiles, nullptr) << error;
        if (!runfiles) {
            return "";
        }
        std::ifstream file(runfiles->Rlocation(rlocation), std::ios::binary);
        EXPECT_TRUE(file.good()) << "missing fixture " << rlocation;
        std::stringstream contents;
        contents << file.rdbuf();
        return contents.str();
    }
};

TEST_P(GoldenFrameTest, MatchesTextFormat) {
    const std::string base = "mwlib/src/test/resources/chassis_proxy/" + GetParam();
    const std::string bytes = readFile(base + ".binpb");

    v1::Envelope expected;
    ASSERT_TRUE(google::protobuf::TextFormat::ParseFromString(readFile(base + ".txtpb"), &expected));

    v1::Envelope decoded;
    ASSERT_TRUE(decoded.ParseFromString(bytes));
    EXPECT_EQ(decoded.protocol_version(), v1::PROTOCOL_VERSION_CURRENT);
    EXPECT_TRUE(MessageDifferencer::Equals(decoded, expected)) << decoded.DebugString();
    EXPECT_EQ(expected.SerializeAsString(), bytes);

    v1::Envelope from_robot;
    ASSERT_TRUE(from_robot.ParseFromString(readFile(base + ".quickbuf.binpb")));
    EXPECT_TRUE(MessageDifferencer::Equals(from_robot, expected)) << from_robot.DebugString();
}

INSTANTIATE_TEST_SUITE_P(MwLib, GoldenFrameTest,
                         ::testing::Values("timesync_request", "timesync_response", "tag_solution", "piece_detections",
                                           "odometry", "module_states", "match_info", "auto_snapshot"));

}  // namespace
}  // namespace proxy_client
