#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <vector>

#include "pcap_recorder/pcap_stream.hpp"

using basin::pcap_recorder::chunkPackets;
using basin::pcap_recorder::PacketBuffer;
using basin::pcap_recorder::PcapPacket;
using basin::pcap_recorder::PcapPacketPtr;
using basin::pcap_recorder::PcapStreamParser;

namespace {

void appendU32(std::vector<uint8_t>& out, uint32_t value, bool swap = false) {
    if (swap) {
        value = __builtin_bswap32(value);
    }
    uint8_t bytes[4];
    std::memcpy(bytes, &value, 4);
    out.insert(out.end(), bytes, bytes + 4);
}

std::vector<uint8_t> fileHeader(uint32_t magic = 0xa1b2c3d4, bool swap = false, uint32_t link_type = 1) {
    std::vector<uint8_t> out;
    appendU32(out, magic, swap);
    appendU32(out, 2 | (4u << 16), swap);
    appendU32(out, 0, swap);
    appendU32(out, 0, swap);
    appendU32(out, 65535, swap);
    appendU32(out, link_type, swap);
    return out;
}

std::vector<uint8_t> record(uint32_t seconds, uint32_t fraction, std::vector<uint8_t> payload, bool swap = false) {
    std::vector<uint8_t> out;
    appendU32(out, seconds, swap);
    appendU32(out, fraction, swap);
    appendU32(out, static_cast<uint32_t>(payload.size()), swap);
    appendU32(out, static_cast<uint32_t>(payload.size()), swap);
    out.insert(out.end(), payload.begin(), payload.end());
    return out;
}

std::vector<uint8_t> concat(std::initializer_list<std::vector<uint8_t>> parts) {
    std::vector<uint8_t> out;
    for (const auto& part : parts) {
        out.insert(out.end(), part.begin(), part.end());
    }
    return out;
}

PcapPacketPtr packet(std::shared_ptr<const std::vector<uint8_t>> header, int64_t timestamp_ns, std::size_t size) {
    auto p = std::make_shared<PcapPacket>();
    p->file_header = std::move(header);
    p->record.assign(size, 0);
    p->timestamp_ns = timestamp_ns;
    return p;
}

}  // namespace

TEST(PcapStreamParser, ParsesAStreamFedOneByteAtATime) {
    const auto stream = concat({ fileHeader(), record(10, 5, { 1, 2, 3 }), record(11, 0, { 4 }) });

    PcapStreamParser parser;
    std::vector<PcapPacketPtr> packets;
    for (uint8_t byte : stream) {
        parser.feed(&byte, 1, [&](PcapPacketPtr p) { packets.push_back(std::move(p)); });
    }

    ASSERT_EQ(packets.size(), 2u);
    EXPECT_EQ(parser.linkType(), 1u);
    EXPECT_EQ(parser.snaplen(), 65535u);
    EXPECT_EQ(*packets[0]->file_header, fileHeader());
    EXPECT_EQ(packets[0]->record, record(10, 5, { 1, 2, 3 }));
    EXPECT_EQ(packets[0]->timestamp_ns, 10'000'005'000);
    EXPECT_EQ(packets[1]->timestamp_ns, 11'000'000'000);
    EXPECT_EQ(packets[0]->file_header, packets[1]->file_header);
}

TEST(PcapStreamParser, HandlesNanosecondAndSwappedStreams) {
    const auto stream = concat({ fileHeader(0xa1b23c4d, true, 113), record(3, 7, { 9, 9 }, true) });

    PcapStreamParser parser;
    std::vector<PcapPacketPtr> packets;
    parser.feed(stream.data(), stream.size(), [&](PcapPacketPtr p) { packets.push_back(std::move(p)); });

    ASSERT_EQ(packets.size(), 1u);
    EXPECT_EQ(parser.linkType(), 113u);
    EXPECT_EQ(packets[0]->timestamp_ns, 3'000'000'007);
    // Kept in the stream's own byte order, so it stays valid next to its header
    EXPECT_EQ(packets[0]->record, record(3, 7, { 9, 9 }, true));
}

TEST(PcapStreamParser, RejectsAStreamThatIsNotPcap) {
    std::vector<uint8_t> junk(64, 'x');
    PcapStreamParser parser;
    EXPECT_THROW(parser.feed(junk.data(), junk.size(), [](PcapPacketPtr) {}), std::runtime_error);

    // ...and works again after a reset
    parser.reset();
    const auto stream = concat({ fileHeader(), record(1, 0, { 1 }) });
    int count = 0;
    parser.feed(stream.data(), stream.size(), [&](PcapPacketPtr) { count++; });
    EXPECT_EQ(count, 1);
}

TEST(PcapStreamParser, RejectsAnImplausibleRecordLength) {
    auto stream = fileHeader();
    appendU32(stream, 0);
    appendU32(stream, 0);
    appendU32(stream, 0x7fffffff);
    appendU32(stream, 0x7fffffff);
    PcapStreamParser parser;
    EXPECT_THROW(parser.feed(stream.data(), stream.size(), [](PcapPacketPtr) {}), std::runtime_error);
}

TEST(PacketBuffer, DropsTheOldestPastMaxBytes) {
    auto header = std::make_shared<const std::vector<uint8_t>>(fileHeader());
    PacketBuffer buffer(100, 0);
    for (int i = 0; i < 5; ++i) {
        buffer.push(packet(header, i, 30));
    }
    EXPECT_EQ(buffer.size(), 3u);
    EXPECT_EQ(buffer.bytes(), 90u);

    auto drained = buffer.drain();
    ASSERT_EQ(drained.size(), 3u);
    EXPECT_EQ(drained.front()->timestamp_ns, 2);
    EXPECT_EQ(drained.back()->timestamp_ns, 4);
    EXPECT_EQ(buffer.size(), 0u);
    EXPECT_EQ(buffer.bytes(), 0u);
}

TEST(PacketBuffer, DropsPacketsOlderThanMaxAge) {
    auto header = std::make_shared<const std::vector<uint8_t>>(fileHeader());
    PacketBuffer buffer(1000, 10);
    for (int i = 0; i < 20; ++i) {
        buffer.push(packet(header, i, 1));
    }
    buffer.evictExpired(25);
    ASSERT_EQ(buffer.size(), 5u);
    EXPECT_EQ(buffer.drain().front()->timestamp_ns, 15);
}

TEST(PacketBuffer, NoAgeLimitWhenMaxAgeIsZero) {
    auto header = std::make_shared<const std::vector<uint8_t>>(fileHeader());
    PacketBuffer buffer(1000, 0);
    buffer.push(packet(header, 0, 1));
    buffer.evictExpired(1'000'000'000'000);
    EXPECT_EQ(buffer.size(), 1u);
}

TEST(ChunkPackets, SplitsAtMaxBytesAndAtHeaderChanges) {
    auto first = std::make_shared<const std::vector<uint8_t>>(fileHeader(0xa1b2c3d4, false, 1));
    auto second = std::make_shared<const std::vector<uint8_t>>(fileHeader(0xa1b2c3d4, false, 113));
    // 24-byte header + 2 x 30-byte records fit in 90; a third doesn't
    std::deque<PcapPacketPtr> packets{ packet(first, 1, 30), packet(first, 2, 30), packet(first, 3, 30),
                                       packet(second, 4, 30), packet(first, 5, 200) };

    auto chunks = chunkPackets(packets, 90);
    ASSERT_EQ(chunks.size(), 4u);
    EXPECT_EQ(chunks[0].packet_count, 2u);
    EXPECT_EQ(chunks[0].data.size(), 24u + 60u);
    EXPECT_EQ(chunks[0].first_timestamp_ns, 1);
    EXPECT_EQ(chunks[0].last_timestamp_ns, 2);
    EXPECT_EQ(chunks[1].packet_count, 1u);
    EXPECT_EQ(chunks[2].packet_count, 1u);
    EXPECT_TRUE(std::equal(second->begin(), second->end(), chunks[2].data.begin()));
    // Oversized on its own: still sent, alone
    EXPECT_EQ(chunks[3].packet_count, 1u);
    EXPECT_EQ(chunks[3].data.size(), 224u);
}

TEST(ChunkPackets, NothingForNoPackets) { EXPECT_TRUE(chunkPackets({}, 100).empty()); }
