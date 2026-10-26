#pragma once

#include <cstdint>

namespace basin::node_core::determinism {

/// @brief Kind of entity an executor dispatch refers to. Mirrors the `KIND_*` constants on
/// `process_msgs::msg::DispatchLogEntry` — kept as a C++ enum class here so call sites
/// (`DeterministicExecutor`'s dispatch classification, `switch` exhaustiveness) don't have to
/// work in terms of raw `uint8_t` message field values.
enum class EntityKind : uint8_t { kTimer = 0, kSubscription = 1, kService = 2, kClient = 3, kWaitable = 4 };

}  // namespace basin::node_core::determinism
