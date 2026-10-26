#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace basin::pcap_recorder {

/// Size of a pcap file's global header, and of each packet record's header.
constexpr std::size_t PCAP_FILE_HEADER_SIZE = 24;
constexpr std::size_t PCAP_RECORD_HEADER_SIZE = 16;

/// One captured packet, kept as the exact bytes tcpdump wrote so it can be written back out unchanged.
struct PcapPacket {
    /// The 24-byte global header of the stream this packet came from. Packets of one tcpdump run share it.
    std::shared_ptr<const std::vector<uint8_t>> file_header;

    /// The 16-byte record header followed by the captured bytes, in the stream's byte order.
    std::vector<uint8_t> record;

    /// Capture time, nanoseconds since the epoch.
    int64_t timestamp_ns{ 0 };
};

using PcapPacketPtr = std::shared_ptr<const PcapPacket>;

/// Splits a pcap byte stream (what `tcpdump -w -` writes) into packets, however the stream is cut into reads.
/// Handles both byte orders and both microsecond and nanosecond timestamps; pcapng is not supported (tcpdump
/// never writes it).
class PcapStreamParser {
   public:
    using PacketCallback = std::function<void(PcapPacketPtr)>;

    /// @brief Consumes the next bytes of the stream, calling `on_packet` for every packet they complete.
    /// @throws std::runtime_error if the stream isn't pcap or a record is implausibly large. The parser is then
    /// unusable until reset().
    void feed(const uint8_t* data, std::size_t size, const PacketCallback& on_packet);

    /// @brief Forgets everything, ready for the start of a new stream.
    void reset();

    /// @brief The stream's global header, or null until all 24 bytes of it have arrived.
    std::shared_ptr<const std::vector<uint8_t>> fileHeader() const { return file_header_; }

    /// @brief The snapshot length and link type from the global header (0 until it has arrived).
    uint32_t snaplen() const { return snaplen_; }
    uint32_t linkType() const { return link_type_; }

   private:
    uint32_t readU32(const uint8_t* bytes) const;

    std::vector<uint8_t> pending_;
    std::size_t offset_{ 0 };

    std::shared_ptr<const std::vector<uint8_t>> file_header_;
    bool swapped_{ false };
    bool nanosecond_{ false };
    uint32_t snaplen_{ 0 };
    uint32_t link_type_{ 0 };
};

/// The packets captured most recently, bounded by total size and by age.
class PacketBuffer {
   public:
    /// @param max_bytes Most record bytes to hold; the oldest packets go first once it is exceeded.
    /// @param max_age_ns Packets captured longer ago than this are dropped by evictExpired(). 0 keeps them
    /// until max_bytes pushes them out.
    PacketBuffer(std::size_t max_bytes, int64_t max_age_ns);

    /// @brief Adds a packet as the newest, dropping the oldest ones past max_bytes.
    void push(PcapPacketPtr packet);

    /// @brief Drops packets captured before `now_ns - max_age_ns`.
    void evictExpired(int64_t now_ns);

    /// @brief Removes and returns every packet, oldest first.
    std::deque<PcapPacketPtr> drain();

    std::size_t bytes() const { return bytes_; }
    std::size_t size() const { return packets_.size(); }

   private:
    void popFront();

    std::size_t max_bytes_;
    int64_t max_age_ns_;
    std::deque<PcapPacketPtr> packets_;
    std::size_t bytes_{ 0 };
};

/// A run of packets written out as a pcap file.
struct PcapChunk {
    std::vector<uint8_t> data;
    uint32_t packet_count{ 0 };
    int64_t first_timestamp_ns{ 0 };
    int64_t last_timestamp_ns{ 0 };
};

/// @brief Packs packets, in order, into pcap files of at most `max_bytes` each (header included). A packet too big
/// for that on its own still gets a chunk to itself. A new chunk also starts wherever the global header changes
/// (tcpdump restarted with a different link type, say), so every chunk is a valid file.
std::vector<PcapChunk> chunkPackets(const std::deque<PcapPacketPtr>& packets, std::size_t max_bytes);

}  // namespace basin::pcap_recorder
