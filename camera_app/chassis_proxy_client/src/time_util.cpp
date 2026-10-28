#include "chassis_proxy_client/time_util.hpp"

#include <cmath>
#include <cstdint>

namespace proxy_client {

namespace {
constexpr int64_t kNsecPerSec = 1000000000LL;

chassis_proxy::v1::Timestamp makeStamp(int64_t sec, int64_t nanosec) {
    chassis_proxy::v1::Timestamp stamp;
    stamp.set_sec(static_cast<int32_t>(sec));
    stamp.set_nanosec(static_cast<uint32_t>(nanosec));
    return stamp;
}
}  // namespace

chassis_proxy::v1::Timestamp toProto(const builtin_interfaces::msg::Time& time) {
    return makeStamp(time.sec, time.nanosec);
}

chassis_proxy::v1::Timestamp toProto(const rclcpp::Time& time) {
    const int64_t nanoseconds = time.nanoseconds();
    return makeStamp(nanoseconds / kNsecPerSec, nanoseconds % kNsecPerSec);
}

double toSec(const chassis_proxy::v1::Timestamp& stamp) {
    return static_cast<double>(stamp.sec()) + static_cast<double>(stamp.nanosec()) / 1.0e9;
}

chassis_proxy::v1::Timestamp offsetBy(const chassis_proxy::v1::Timestamp& stamp, double offset_sec) {
    double sec_floor = std::floor(offset_sec);
    int64_t delta_sec = static_cast<int64_t>(sec_floor);
    double frac = offset_sec - sec_floor;  // in [0,1)
    int64_t delta_nsec = static_cast<int64_t>(std::llround(frac * 1.0e9));

    int64_t new_sec = static_cast<int64_t>(stamp.sec()) + delta_sec;
    int64_t new_nsec = static_cast<int64_t>(stamp.nanosec()) + delta_nsec;

    if (new_nsec >= kNsecPerSec) {
        new_sec += new_nsec / kNsecPerSec;
        new_nsec = new_nsec % kNsecPerSec;
    } else if (new_nsec < 0) {
        int64_t borrow = (-new_nsec + kNsecPerSec - 1) / kNsecPerSec;
        new_sec -= borrow;
        new_nsec += borrow * kNsecPerSec;
    }

    return makeStamp(new_sec, new_nsec);
}

}  // namespace proxy_client
