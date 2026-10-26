#include "pcap_recorder/pcap_stream.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

namespace basin::pcap_recorder {

namespace {
constexpr uint32_t MAGIC_MICROSECONDS = 0xa1b2c3d4;
constexpr uint32_t MAGIC_NANOSECONDS = 0xa1b23c4d;

// No link layer tcpdump supports captures anywhere near this per packet; a record claiming more means the
// stream is out of step (or isn't pcap at all), and reading on would just buffer garbage.
constexpr uint32_t MAX_RECORD_BYTES = 16 * 1024 * 1024;

uint32_t loadU32(const uint8_t* bytes) {
    uint32_t value;
    std::memcpy(&value, bytes, sizeof(value));
    return value;
}
}  // namespace

uint32_t PcapStreamParser::readU32(const uint8_t* bytes) const {
    uint32_t value = loadU32(bytes);
    return swapped_ ? __builtin_bswap32(value) : value;
}

void PcapStreamParser::reset() {
    pending_.clear();
    offset_ = 0;
    file_header_.reset();
    swapped_ = false;
    nanosecond_ = false;
    snaplen_ = 0;
    link_type_ = 0;
}

void PcapStreamParser::feed(const uint8_t* data, std::size_t size, const PacketCallback& on_packet) {
    pending_.insert(pending_.end(), data, data + size);

    while (true) {
        const std::size_t available = pending_.size() - offset_;
        const uint8_t* cursor = pending_.data() + offset_;

        if (!file_header_) {
            if (available < PCAP_FILE_HEADER_SIZE) {
                break;
            }
            const uint32_t magic = loadU32(cursor);
            if (magic == MAGIC_MICROSECONDS || magic == MAGIC_NANOSECONDS) {
                swapped_ = false;
            } else if (__builtin_bswap32(magic) == MAGIC_MICROSECONDS ||
                       __builtin_bswap32(magic) == MAGIC_NANOSECONDS) {
                swapped_ = true;
            } else {
                throw std::runtime_error("not a pcap stream (magic number " + std::to_string(magic) + ")");
            }
            nanosecond_ = readU32(cursor) == MAGIC_NANOSECONDS;
            snaplen_ = readU32(cursor + 16);
            link_type_ = readU32(cursor + 20);
            file_header_ = std::make_shared<const std::vector<uint8_t>>(cursor, cursor + PCAP_FILE_HEADER_SIZE);
            offset_ += PCAP_FILE_HEADER_SIZE;
            continue;
        }

        if (available < PCAP_RECORD_HEADER_SIZE) {
            break;
        }
        const uint32_t captured_bytes = readU32(cursor + 8);
        if (captured_bytes > MAX_RECORD_BYTES) {
            throw std::runtime_error("pcap record claims " + std::to_string(captured_bytes) + " bytes");
        }
        const std::size_t record_size = PCAP_RECORD_HEADER_SIZE + captured_bytes;
        if (available < record_size) {
            break;
        }

        auto packet = std::make_shared<PcapPacket>();
        packet->file_header = file_header_;
        packet->record.assign(cursor, cursor + record_size);
        const int64_t seconds = readU32(cursor);
        const int64_t fraction = readU32(cursor + 4);
        packet->timestamp_ns = seconds * 1'000'000'000 + (nanosecond_ ? fraction : fraction * 1'000);
        offset_ += record_size;

        on_packet(std::move(packet));
    }

    // Compact once the consumed prefix dominates, so a long stream doesn't keep its whole history in pending_.
    if (offset_ > 0 && offset_ * 2 >= pending_.size()) {
        pending_.erase(pending_.begin(), pending_.begin() + static_cast<std::ptrdiff_t>(offset_));
        offset_ = 0;
    }
}

PacketBuffer::PacketBuffer(std::size_t max_bytes, int64_t max_age_ns) : max_bytes_(max_bytes), max_age_ns_(max_age_ns) {}

void PacketBuffer::push(PcapPacketPtr packet) {
    bytes_ += packet->record.size();
    packets_.push_back(std::move(packet));
    while (bytes_ > max_bytes_ && !packets_.empty()) {
        popFront();
    }
}

void PacketBuffer::evictExpired(int64_t now_ns) {
    if (max_age_ns_ <= 0) {
        return;
    }
    const int64_t cutoff = now_ns - max_age_ns_;
    while (!packets_.empty() && packets_.front()->timestamp_ns < cutoff) {
        popFront();
    }
}

std::deque<PcapPacketPtr> PacketBuffer::drain() {
    std::deque<PcapPacketPtr> drained;
    drained.swap(packets_);
    bytes_ = 0;
    return drained;
}

void PacketBuffer::popFront() {
    bytes_ -= packets_.front()->record.size();
    packets_.pop_front();
}

std::vector<PcapChunk> chunkPackets(const std::deque<PcapPacketPtr>& packets, std::size_t max_bytes) {
    std::vector<PcapChunk> chunks;
    const std::vector<uint8_t>* chunk_header = nullptr;

    for (const PcapPacketPtr& packet : packets) {
        const bool header_changed = chunk_header != packet->file_header.get();
        const bool full = !chunks.empty() && chunks.back().packet_count > 0 &&
                          chunks.back().data.size() + packet->record.size() > max_bytes;
        if (chunks.empty() || header_changed || full) {
            chunk_header = packet->file_header.get();
            PcapChunk& chunk = chunks.emplace_back();
            chunk.data.assign(chunk_header->begin(), chunk_header->end());
            chunk.first_timestamp_ns = packet->timestamp_ns;
        }
        PcapChunk& chunk = chunks.back();
        chunk.data.insert(chunk.data.end(), packet->record.begin(), packet->record.end());
        chunk.packet_count++;
        chunk.last_timestamp_ns = packet->timestamp_ns;
    }
    return chunks;
}

}  // namespace basin::pcap_recorder
