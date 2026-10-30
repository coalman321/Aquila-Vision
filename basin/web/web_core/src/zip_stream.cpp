#include "basin/web_core/zip_stream.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <stdexcept>

namespace basin::web_core {

namespace {

constexpr uint32_t LOCAL_HEADER_SIGNATURE = 0x04034b50;
constexpr uint32_t CENTRAL_HEADER_SIGNATURE = 0x02014b50;
constexpr uint32_t ZIP64_END_SIGNATURE = 0x06064b50;
constexpr uint32_t ZIP64_LOCATOR_SIGNATURE = 0x07064b50;
constexpr uint32_t END_SIGNATURE = 0x06054b50;

constexpr uint16_t ZIP64_EXTRA_ID = 0x0001;
constexpr uint16_t VERSION_DEFAULT = 20;  // 2.0: plain stored entries
constexpr uint16_t VERSION_ZIP64 = 45;    // 4.5: Zip64 extensions
constexpr uint16_t MADE_BY_UNIX = 3 << 8;
constexpr uint16_t FLAG_UTF8_NAME = 1 << 11;
constexpr uint16_t METHOD_STORED = 0;
constexpr uint32_t UNIX_FILE_MODE = 0100644;

constexpr uint32_t MAX_32 = 0xFFFFFFFFU;
constexpr uint16_t MAX_16 = 0xFFFF;

constexpr size_t READ_CHUNK = 1 << 20;

const std::array<uint32_t, 256> CRC_TABLE = [] {
    std::array<uint32_t, 256> table{};
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int k = 0; k < 8; k++) {
            c = (c & 1) ? 0xEDB88320U ^ (c >> 1) : c >> 1;
        }
        table[i] = c;
    }
    return table;
}();

void put16(std::string& out, uint16_t v) {
    out.push_back(static_cast<char>(v & 0xFF));
    out.push_back(static_cast<char>(v >> 8));
}

void put32(std::string& out, uint32_t v) {
    put16(out, static_cast<uint16_t>(v & 0xFFFF));
    put16(out, static_cast<uint16_t>(v >> 16));
}

void put64(std::string& out, uint64_t v) {
    put32(out, static_cast<uint32_t>(v & MAX_32));
    put32(out, static_cast<uint32_t>(v >> 32));
}

/// MS-DOS time and date, as ZIP headers store them, of an epoch time in local time
void toDos(time_t t, uint16_t& dos_time, uint16_t& dos_date) {
    struct tm local {};
    localtime_r(&t, &local);
    // DOS dates start in 1980
    if (local.tm_year < 80) {
        dos_time = 0;
        dos_date = (1 << 5) | 1;
        return;
    }
    dos_time = static_cast<uint16_t>((local.tm_hour << 11) | (local.tm_min << 5) | (local.tm_sec / 2));
    dos_date = static_cast<uint16_t>(((local.tm_year - 80) << 9) | ((local.tm_mon + 1) << 5) | local.tm_mday);
}

/// Reads exactly `length` bytes at `offset`, or throws
void preadFully(int fd, char* buffer, size_t length, uint64_t offset) {
    while (length > 0) {
        ssize_t n = ::pread(fd, buffer, length, static_cast<off_t>(offset));
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            throw std::runtime_error(n == 0 ? "file is shorter than when it was added"
                                            : std::string("read failed: ") + std::strerror(errno));
        }
        buffer += n;
        length -= static_cast<size_t>(n);
        offset += static_cast<uint64_t>(n);
    }
}

}  // namespace

uint32_t crc32Update(uint32_t crc, const void* data, size_t length) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    crc = ~crc;
    for (size_t i = 0; i < length; i++) {
        crc = CRC_TABLE[(crc ^ bytes[i]) & 0xFF] ^ (crc >> 8);
    }
    return ~crc;
}

ZipStream::ZipStream(uint64_t zip64_threshold) : zip64_threshold_(zip64_threshold) {}

ZipStream::~ZipStream() {
    for (const Entry& entry : entries_) {
        if (entry.fd >= 0) {
            ::close(entry.fd);
        }
    }
}

void ZipStream::addFile(const std::string& name, const std::filesystem::path& path) {
    Entry entry;
    entry.name = name;
    entry.fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (entry.fd < 0) {
        throw std::runtime_error("cannot open " + path.string() + ": " + std::strerror(errno));
    }

    try {
        struct stat st {};
        if (::fstat(entry.fd, &st) != 0) {
            throw std::runtime_error("cannot stat " + path.string() + ": " + std::strerror(errno));
        }
        entry.size = static_cast<uint64_t>(st.st_size);
        toDos(st.st_mtime, entry.dos_time, entry.dos_date);

        std::vector<char> buffer(READ_CHUNK);
        for (uint64_t offset = 0; offset < entry.size;) {
            size_t n = static_cast<size_t>(std::min<uint64_t>(READ_CHUNK, entry.size - offset));
            preadFully(entry.fd, buffer.data(), n, offset);
            entry.crc = crc32Update(entry.crc, buffer.data(), n);
            offset += n;
        }
    } catch (const std::runtime_error& e) {
        ::close(entry.fd);
        throw std::runtime_error(path.string() + ": " + e.what());
    }

    addEntry(std::move(entry));
}

void ZipStream::addData(const std::string& name, std::string data) {
    Entry entry;
    entry.name = name;
    entry.size = data.size();
    entry.crc = crc32Update(0, data.data(), data.size());
    entry.data = std::move(data);
    toDos(std::time(nullptr), entry.dos_time, entry.dos_date);
    addEntry(std::move(entry));
}

void ZipStream::addEntry(Entry entry) {
    if (finished_) {
        if (entry.fd >= 0) {
            ::close(entry.fd);
        }
        throw std::logic_error("ZipStream: entry added after finish()");
    }
    entries_.push_back(std::move(entry));
}

bool ZipStream::needsZip64(const Entry& entry) const {
    return entry.size >= zip64_threshold_ || entry.header_offset >= zip64_threshold_;
}

void ZipStream::finish() {
    if (finished_) {
        return;
    }
    finished_ = true;

    // Each entry: its local header, then its content
    uint64_t offset = 0;
    for (Entry& entry : entries_) {
        entry.header_offset = offset;
        // A local header can't know its own offset will need Zip64, so only the size decides it here
        bool zip64 = entry.size >= zip64_threshold_;

        std::string header;
        put32(header, LOCAL_HEADER_SIGNATURE);
        put16(header, zip64 ? VERSION_ZIP64 : VERSION_DEFAULT);
        put16(header, FLAG_UTF8_NAME);
        put16(header, METHOD_STORED);
        put16(header, entry.dos_time);
        put16(header, entry.dos_date);
        put32(header, entry.crc);
        put32(header, zip64 ? MAX_32 : static_cast<uint32_t>(entry.size));  // compressed size
        put32(header, zip64 ? MAX_32 : static_cast<uint32_t>(entry.size));  // uncompressed size
        put16(header, static_cast<uint16_t>(entry.name.size()));
        put16(header, zip64 ? 20 : 0);
        header += entry.name;
        if (zip64) {
            put16(header, ZIP64_EXTRA_ID);
            put16(header, 16);
            put64(header, entry.size);  // uncompressed
            put64(header, entry.size);  // compressed
        }

        segments_.push_back(Segment{ offset, header.size(), header, nullptr });
        offset += header.size();
        segments_.push_back(Segment{ offset, entry.size, {}, &entry });
        offset += entry.size;
    }

    // The central directory, then the end records
    uint64_t central_offset = offset;
    std::string central;
    bool any_zip64 = false;
    for (const Entry& entry : entries_) {
        bool zip64_size = entry.size >= zip64_threshold_;
        bool zip64_offset = entry.header_offset >= zip64_threshold_;
        any_zip64 = any_zip64 || needsZip64(entry);

        std::string extra;
        if (zip64_size || zip64_offset) {
            std::string fields;
            if (zip64_size) {
                put64(fields, entry.size);  // uncompressed
                put64(fields, entry.size);  // compressed
            }
            if (zip64_offset) {
                put64(fields, entry.header_offset);
            }
            put16(extra, ZIP64_EXTRA_ID);
            put16(extra, static_cast<uint16_t>(fields.size()));
            extra += fields;
        }

        put32(central, CENTRAL_HEADER_SIGNATURE);
        put16(central, MADE_BY_UNIX | (needsZip64(entry) ? VERSION_ZIP64 : VERSION_DEFAULT));
        put16(central, needsZip64(entry) ? VERSION_ZIP64 : VERSION_DEFAULT);
        put16(central, FLAG_UTF8_NAME);
        put16(central, METHOD_STORED);
        put16(central, entry.dos_time);
        put16(central, entry.dos_date);
        put32(central, entry.crc);
        put32(central, zip64_size ? MAX_32 : static_cast<uint32_t>(entry.size));
        put32(central, zip64_size ? MAX_32 : static_cast<uint32_t>(entry.size));
        put16(central, static_cast<uint16_t>(entry.name.size()));
        put16(central, static_cast<uint16_t>(extra.size()));
        put16(central, 0);  // comment length
        put16(central, 0);  // disk number start
        put16(central, 0);  // internal attributes
        put32(central, UNIX_FILE_MODE << 16);
        put32(central, zip64_offset ? MAX_32 : static_cast<uint32_t>(entry.header_offset));
        central += entry.name;
        central += extra;
    }

    uint64_t central_size = central.size();
    uint64_t count = entries_.size();
    bool zip64_end = any_zip64 || count >= MAX_16 || central_offset >= zip64_threshold_ ||
                     central_size >= zip64_threshold_;

    std::string end = std::move(central);
    if (zip64_end) {
        uint64_t zip64_end_offset = central_offset + central_size;
        put32(end, ZIP64_END_SIGNATURE);
        put64(end, 44);  // size of the rest of this record
        put16(end, MADE_BY_UNIX | VERSION_ZIP64);
        put16(end, VERSION_ZIP64);
        put32(end, 0);  // this disk
        put32(end, 0);  // disk with the central directory
        put64(end, count);
        put64(end, count);
        put64(end, central_size);
        put64(end, central_offset);

        put32(end, ZIP64_LOCATOR_SIGNATURE);
        put32(end, 0);  // disk with the Zip64 end record
        put64(end, zip64_end_offset);
        put32(end, 1);  // total disks
    }
    put32(end, END_SIGNATURE);
    put16(end, 0);  // this disk
    put16(end, 0);  // disk with the central directory
    put16(end, zip64_end ? MAX_16 : static_cast<uint16_t>(count));
    put16(end, zip64_end ? MAX_16 : static_cast<uint16_t>(count));
    put32(end, zip64_end ? MAX_32 : static_cast<uint32_t>(central_size));
    put32(end, zip64_end ? MAX_32 : static_cast<uint32_t>(central_offset));
    put16(end, 0);  // comment length

    segments_.push_back(Segment{ central_offset, end.size(), end, nullptr });
    total_size_ = central_offset + end.size();
}

size_t ZipStream::read(uint64_t offset, char* buffer, size_t length) const {
    if (!finished_) {
        throw std::logic_error("ZipStream: read() before finish()");
    }

    size_t copied = 0;
    // The first segment that ends after offset
    auto it = std::upper_bound(segments_.begin(), segments_.end(), offset,
                               [](uint64_t value, const Segment& s) { return value < s.offset + s.length; });
    for (; it != segments_.end() && copied < length; ++it) {
        uint64_t within = offset + copied - it->offset;
        size_t n = static_cast<size_t>(std::min<uint64_t>(it->length - within, length - copied));
        if (n == 0) {
            continue;
        }
        if (it->entry == nullptr) {
            std::memcpy(buffer + copied, it->bytes.data() + within, n);
        } else if (it->entry->fd < 0) {
            std::memcpy(buffer + copied, it->entry->data.data() + within, n);
        } else {
            preadFully(it->entry->fd, buffer + copied, n, within);
        }
        copied += n;
    }
    return copied;
}

}  // namespace basin::web_core
