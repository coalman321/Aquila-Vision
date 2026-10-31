#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace basin::web_core {

/// @brief CRC-32 (ISO-HDLC, the one ZIP uses) of a buffer, continuing from `crc`
/// @param crc The CRC so far, 0 to start
uint32_t crc32Update(uint32_t crc, const void* data, size_t length);

/// @brief A ZIP archive of uncompressed (stored) entries, laid out up front and read back by offset
///
/// Built for streaming large files over HTTP: every file is opened, measured and checksummed as it is
/// added, so the archive's exact size is known before the first byte is sent, and its headers need no
/// data descriptors (which some unzippers handle poorly). read() then serves any byte range, reading
/// file contents straight from the descriptors opened when they were added. Those stay open until the
/// stream is destroyed, so a file deleted mid-download is still sent whole.
///
/// Zip64 records are written only for what needs them: an entry, offset or central directory at or
/// over `zip64_threshold` bytes (4 GiB by default; lower only to test).
///
/// Not thread safe: one stream serves one download.
class ZipStream {
   public:
    explicit ZipStream(uint64_t zip64_threshold = 0xFFFFFFFFULL);
    ~ZipStream();

    ZipStream(const ZipStream&) = delete;
    ZipStream& operator=(const ZipStream&) = delete;

    /// @brief Adds a file, reading it once to checksum it. Only its current size is ever sent
    /// @param name The entry's name in the archive, with '/' separating directories
    /// @param path The file to add
    /// @param on_read Called as the checksum pass reads the file, with the bytes read since the last call
    /// @throws std::runtime_error if the file can't be opened or read
    void addFile(const std::string& name, const std::filesystem::path& path,
                 const std::function<void(uint64_t)>& on_read = {});

    /// @brief Adds an entry whose content is held in memory
    void addData(const std::string& name, std::string data);

    /// @brief Lays out the central directory. Call after the last add and before size() or read()
    void finish();

    /// @brief The archive's total size in bytes
    uint64_t size() const { return total_size_; }

    /// @brief Copies up to `length` bytes of the archive, starting at `offset`, into `buffer`
    /// @return The number of bytes copied, 0 at or past the end
    /// @throws std::runtime_error if a file can't be read
    size_t read(uint64_t offset, char* buffer, size_t length) const;

   private:
    struct Entry {
        std::string name;
        int fd{ -1 };      ///< The open file, or -1 for an in-memory entry
        std::string data;  ///< The content of an in-memory entry
        uint64_t size{ 0 };
        uint32_t crc{ 0 };
        uint16_t dos_time{ 0 };
        uint16_t dos_date{ 0 };
        uint64_t header_offset{ 0 };
    };

    /// @brief One contiguous part of the archive: header bytes, or an entry's content
    struct Segment {
        uint64_t offset{ 0 };
        uint64_t length{ 0 };
        std::string bytes;            ///< Header bytes, when entry is null
        const Entry* entry{ nullptr };  ///< The entry whose content this is
    };

    void addEntry(Entry entry);

    bool needsZip64(const Entry& entry) const;

    uint64_t zip64_threshold_;
    std::vector<Entry> entries_;
    std::vector<Segment> segments_;
    uint64_t total_size_{ 0 };
    bool finished_{ false };
};

}  // namespace basin::web_core
