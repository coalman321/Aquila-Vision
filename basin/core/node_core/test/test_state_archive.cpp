#include <gtest/gtest.h>

#include <basin/node_core/determinism/state_archive.hpp>

using basin::node_core::determinism::StateArchive;

TEST(StateArchive, PutGetRoundTrip) {
    StateArchive archive;
    archive.put("counter", 42);
    archive.put("label", std::string("boom_joint"));
    archive.put("gain", 1.5);

    EXPECT_EQ(archive.get<int>("counter"), 42);
    EXPECT_EQ(archive.get<std::string>("label"), "boom_joint");
    EXPECT_DOUBLE_EQ(archive.get<double>("gain"), 1.5);
}

TEST(StateArchive, HasReflectsPresence) {
    StateArchive archive;
    EXPECT_FALSE(archive.has("missing"));
    archive.put("present", 1);
    EXPECT_TRUE(archive.has("present"));
}

TEST(StateArchive, GetOrFallsBackWhenAbsent) {
    StateArchive archive;
    EXPECT_EQ(archive.getOr<int>("missing", 7), 7);
    archive.put("present", 3);
    EXPECT_EQ(archive.getOr<int>("present", 7), 3);
}

TEST(StateArchive, DumpLoadRoundTrip) {
    StateArchive archive;
    archive.put("counter", 42);
    archive.put("label", std::string("boom_joint"));

    std::string serialized = archive.dump();

    StateArchive restored;
    restored.load(serialized);

    EXPECT_EQ(restored.get<int>("counter"), 42);
    EXPECT_EQ(restored.get<std::string>("label"), "boom_joint");
}

TEST(StateArchive, GetMissingKeyThrows) {
    StateArchive archive;
    EXPECT_THROW(archive.get<int>("missing"), nlohmann::json::out_of_range);
}
