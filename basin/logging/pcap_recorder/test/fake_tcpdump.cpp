// Stands in for tcpdump in tests: writes a pcap stream to stdout, one 8-byte packet every 10 ms whose payload is
// a big-endian counter from 0, until it is signalled. Writes a "listening" line to stderr first, as tcpdump does,
// and ignores its arguments.
#include <signal.h>
#include <unistd.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {
volatile sig_atomic_t stop = 0;

void onSignal(int) { stop = 1; }

bool writeAll(const void* data, size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    while (size > 0) {
        ssize_t count = write(STDOUT_FILENO, bytes, size);
        if (count <= 0) {
            return false;
        }
        bytes += count;
        size -= static_cast<size_t>(count);
    }
    return true;
}
}  // namespace

int main() {
    signal(SIGTERM, onSignal);
    signal(SIGINT, onSignal);

    const uint32_t file_header[6] = { 0xa1b2c3d4, 2 | (4u << 16), 0, 0, 65535, 1 };
    if (!writeAll(file_header, sizeof(file_header))) {
        return 1;
    }
    std::fprintf(stderr, "listening on fake0, link-type EN10MB (Ethernet), snapshot length 65535 bytes\n");

    uint64_t counter = 0;
    while (!stop) {
        const auto now = std::chrono::system_clock::now().time_since_epoch();
        const auto us = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
        uint8_t record[16 + 8];
        const uint32_t header[4] = { static_cast<uint32_t>(us / 1000000), static_cast<uint32_t>(us % 1000000), 8, 8 };
        std::memcpy(record, header, sizeof(header));
        for (int i = 0; i < 8; ++i) {
            record[16 + i] = static_cast<uint8_t>(counter >> (56 - 8 * i));
        }
        if (!writeAll(record, sizeof(record))) {
            return 1;
        }
        counter++;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    std::fprintf(stderr, "%llu packets captured\n", static_cast<unsigned long long>(counter));
    return 0;
}
