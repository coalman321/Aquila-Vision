#pragma once

#include <builtin_interfaces/msg/time.hpp>
#include <rclcpp/time.hpp>

#include "chassis_proxy/v1/core.pb.h"

namespace proxy_client {

chassis_proxy::v1::Timestamp toProto(const builtin_interfaces::msg::Time& time);

chassis_proxy::v1::Timestamp toProto(const rclcpp::Time& time);

double toSec(const chassis_proxy::v1::Timestamp& stamp);

/// Shifts a timestamp by offset_sec (which may be negative), keeping nanosec in [0, 1e9)
chassis_proxy::v1::Timestamp offsetBy(const chassis_proxy::v1::Timestamp& stamp, double offset_sec);

}  // namespace proxy_client
