#include <fcntl.h>
#include <gtest/gtest.h>
#include <sys/file.h>
#include <unistd.h>

#include <cstdlib>
#include <random>
#include <rclcpp/rclcpp.hpp>
#include <string>

namespace {

/// @brief Picks a DDS domain ID that no other live test process on this host is using.
///
/// These tests create real publishers/subscribers, and Bazel runs test processes concurrently
/// (this target's own shards/runs, plus every other DDS-using target). On a shared domain those
/// processes discover each other and their receive threads process each other's traffic, which
/// makes tests see foreign messages and, worse, deadlocks Fast DDS internally while entities are
/// created/destroyed (hangs in `create_datawriter` and `delete_datareader`).
///
/// Each domain ID is claimed with a non-blocking `flock` on a per-ID file that this process holds
/// open for its whole lifetime; the kernel drops the lock when the process exits, however it
/// exits. IDs 0-101 are the range ROS 2 documents as safe for the default port mapping. If every
/// ID is claimed (or the lock files can't be used) it falls back to a random one, which is no worse
/// than sharing a domain.
size_t claimIsolatedDomainId() {
    constexpr size_t kDomainCount = 102;
    const size_t start = std::random_device{}() % kDomainCount;
    for (size_t i = 0; i < kDomainCount; ++i) {
        const size_t domain_id = (start + i) % kDomainCount;
        const std::string lock_path = "/tmp/basin_test_ros_domain_" + std::to_string(domain_id) + ".lock";
        const int fd = ::open(lock_path.c_str(), O_CREAT | O_RDWR | O_CLOEXEC, 0666);
        if (fd < 0) {
            continue;
        }
        if (::flock(fd, LOCK_EX | LOCK_NB) == 0) {
            return domain_id;  // `fd` intentionally left open: closing it would release the claim
        }
        ::close(fd);
    }
    return start;
}

}  // namespace

int main(int argc, char** argv) {
    testing::InitGoogleTest(&argc, argv);

    rclcpp::InitOptions init_options;
    // An explicit ROS_DOMAIN_ID is a deliberate choice (e.g. debugging against a live system) and wins.
    if (std::getenv("ROS_DOMAIN_ID") == nullptr) {
        init_options.set_domain_id(claimIsolatedDomainId());
    }
    rclcpp::init(argc, argv, init_options);

    int result = RUN_ALL_TESTS();
    rclcpp::shutdown();
    return result;
}
