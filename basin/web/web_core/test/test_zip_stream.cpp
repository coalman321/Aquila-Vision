#include <gtest/gtest.h>
#include <unistd.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <stdexcept>
#include <string>

#include "basin/web_core/zip_stream.hpp"

using basin::web_core::crc32Update;
using basin::web_core::ZipStream;

namespace {

std::string readFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

void writeFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream out(path, std::ios::binary);
    out << content;
}

std::string randomBytes(size_t n, unsigned seed) {
    std::mt19937 gen(seed);
    std::string s(n, '\0');
    for (char& c : s) {
        c = static_cast<char>(gen() & 0xFF);
    }
    return s;
}

/// Reads a whole stream with one read() call
std::string readAll(const ZipStream& zip) {
    std::string out(zip.size(), '\0');
    EXPECT_EQ(zip.read(0, out.data(), out.size()), out.size());
    return out;
}

bool haveCommand(const std::string& command) {
    return std::system(("command -v " + command + " > /dev/null 2>&1").c_str()) == 0;
}

/// Writes ZipStreams over a scratch directory and checks what real unzippers make of them
class ZipStreamTest : public ::testing::Test {
   protected:
    void SetUp() override {
        dir_ = std::filesystem::temp_directory_path() /
               ("zip_stream_test_" + std::to_string(::getpid()) + "_" +
                ::testing::UnitTest::GetInstance()->current_test_info()->name());
        std::filesystem::remove_all(dir_);
        std::filesystem::create_directories(dir_ / "in");
        files_ = {
            { "snap/log_0_000000.mcap", randomBytes(300000, 1) },
            { "snap/log_0_000001.mcap", randomBytes(70001, 2) },
            { "snap/empty.mcap", "" },
        };
        for (const auto& [name, content] : files_) {
            writeFile(input(name), content);
        }
    }

    void TearDown() override { std::filesystem::remove_all(dir_); }

    std::filesystem::path input(const std::string& name) const {
        return dir_ / "in" / std::filesystem::path(name).filename();
    }

    void addInputs(ZipStream& zip) {
        for (const auto& [name, content] : files_) {
            zip.addFile(name, input(name));
        }
        zip.addData("snap/snapshot.yaml", "id: snap\n");
    }

    /// Extracts `archive` with Python's zipfile, after its own CRC check, and checks every entry came out intact
    void expectPythonExtracts(const std::string& archive) {
        if (!haveCommand("python3")) {
            GTEST_SKIP() << "python3 is not available";
        }
        std::filesystem::path zip_path = dir_ / "out.zip";
        writeFile(zip_path, archive);
        std::filesystem::path out_dir = dir_ / "out";
        std::string script = "import sys, zipfile\n"
                             "z = zipfile.ZipFile(sys.argv[1])\n"
                             "assert z.testzip() is None\n"
                             "z.extractall(sys.argv[2])\n";
        writeFile(dir_ / "extract.py", script);
        ASSERT_EQ(std::system(("python3 " + (dir_ / "extract.py").string() + " " + zip_path.string() + " " +
                               out_dir.string())
                                  .c_str()),
                  0);

        for (const auto& [name, content] : files_) {
            EXPECT_EQ(readFile(out_dir / name), content) << name;
        }
        EXPECT_EQ(readFile(out_dir / "snap/snapshot.yaml"), "id: snap\n");
    }

    std::filesystem::path dir_;
    std::map<std::string, std::string> files_;
};

}  // namespace

TEST(Crc32, MatchesTheStandardCheckValue) {
    EXPECT_EQ(crc32Update(0, "123456789", 9), 0xCBF43926U);
    EXPECT_EQ(crc32Update(0, "", 0), 0U);
    // Continuing a CRC over a split buffer gives the same result
    EXPECT_EQ(crc32Update(crc32Update(0, "1234", 4), "56789", 5), 0xCBF43926U);
}

TEST_F(ZipStreamTest, IsExactlyItsAdvertisedSizeAndReadsTheSameInAnySlices) {
    ZipStream zip;
    addInputs(zip);
    zip.finish();

    std::string whole = readAll(zip);
    ASSERT_EQ(whole.size(), zip.size());
    char past_end;
    EXPECT_EQ(zip.read(zip.size(), &past_end, 1), 0u);

    // Slices of varied sizes, each crossing header and content boundaries
    std::string sliced;
    std::mt19937 gen(7);
    while (sliced.size() < zip.size()) {
        std::string chunk(1 + gen() % 9000, '\0');
        size_t n = zip.read(sliced.size(), chunk.data(), chunk.size());
        ASSERT_GT(n, 0u);
        sliced.append(chunk, 0, n);
    }
    EXPECT_EQ(sliced, whole);

    // Stored, so the archive is the content plus modest headers
    size_t content = 9;
    for (const auto& [name, data] : files_) {
        content += data.size();
    }
    EXPECT_LT(whole.size() - content, 1024u);
}

TEST_F(ZipStreamTest, ExtractsWithPythonAndUnzip) {
    ZipStream zip;
    addInputs(zip);
    zip.finish();
    std::string archive = readAll(zip);

    expectPythonExtracts(archive);

    if (haveCommand("unzip")) {
        EXPECT_EQ(std::system(("unzip -tq " + (dir_ / "out.zip").string() + " > /dev/null").c_str()), 0);
    }
}

TEST_F(ZipStreamTest, WritesZip64RecordsOverTheThreshold) {
    // A threshold every entry and offset but the first is over
    ZipStream zip(1000);
    addInputs(zip);
    zip.finish();
    std::string archive = readAll(zip);

    EXPECT_NE(archive.find(std::string("PK\x06\x06", 4)), std::string::npos) << "no Zip64 end record";
    EXPECT_NE(archive.find(std::string("PK\x06\x07", 4)), std::string::npos) << "no Zip64 end locator";
    expectPythonExtracts(archive);
}

TEST_F(ZipStreamTest, StillSendsAFileDeletedAfterItWasAdded) {
    ZipStream zip;
    addInputs(zip);
    zip.finish();
    for (const auto& [name, content] : files_) {
        std::filesystem::remove(input(name));
    }

    expectPythonExtracts(readAll(zip));
}

TEST_F(ZipStreamTest, ReportsTheChecksumPassReadingEachFile) {
    ZipStream zip;
    const std::string content = files_.at("snap/log_0_000000.mcap");
    uint64_t read = 0;
    int calls = 0;
    zip.addFile("big", input("snap/log_0_000000.mcap"), [&](uint64_t n) {
        read += n;
        calls++;
    });
    EXPECT_EQ(read, content.size());
    EXPECT_GE(calls, 1);
}

TEST_F(ZipStreamTest, RejectsAMissingFile) {
    ZipStream zip;
    EXPECT_THROW(zip.addFile("missing", dir_ / "in" / "missing.mcap"), std::runtime_error);
}
