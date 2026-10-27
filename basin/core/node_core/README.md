# node_core

Lightweight reusable core for basin nodes — provides a base `NodeCore` class,
heartbeat utilities, and common parameter helpers to simplify writing ROS 2
components and nodes.

## Overview

`node_core` is a small ament_cmake package that builds a shared library
(`libnode_core`) exporting utilities used across the `basin` codebase:

- `basin::node_core::NodeCore` — an abstract base class derived from
	`rclcpp::Node` that adds a fixed-rate tick callback and an integrated
	heartbeat sender.
- `basin::monitor_core::HeartbeatSender` — helper for publishing
	`process_msgs::msg::ProcessHeartbeat` and managing shutdown heartbeats.
- `basin::node_core::NodeParams` and macros (`BASIN_PARAM`, `BASIN_DYN_PARAM`)
	— helpers to declare and manage parameters (including dynamic updates).
- `basin::node_core::Debouncer<T>` — time-stable debouncer for discrete
	signals (limit switches, mode flags): a new value is only committed once it
	has persisted for a configured settle duration.

The package builds a shared library (no standalone executable). It's
intended to be linked into node implementations or component libraries.

## Contents / API summary

- `include/basin/node_core/node_core.hpp`
	- `class NodeCore` — constructor accepts `node_name`, `namespace`, and
		`rclcpp::NodeOptions`. Override `onTick()` for periodic work and
		`shutdown()` to handle shutdown.
- `include/basin/node_core/node_monitor.hpp`
	- `class HeartbeatSender` — construct with an `rclcpp::Node*`. Call
		`startBeatTimer()` to run an internal timer or `sendBeat()` to send a
		heartbeat manually.
- `include/basin/node_core/node_params.hpp`
	- `class NodeParams` — base for managing parameter event handlers.
	- `BASIN_PARAM` / `BASIN_DYN_PARAM` macros — convenience macros for
		declaring parameters and wiring callbacks for dynamic updates.
- `include/basin/node_core/param_client.hpp`
	- `class ParamClient` — blocking client for setting parameters on remote
		ROS2 nodes. Provides methods to check node availability and set
		parameters synchronously.
- `include/basin/node_core/debouncer.hpp`
	- `template <typename T = bool> class Debouncer` — header-only time-stable
		debouncer. `update(sample, now)` returns whether the committed value
		changed; see the "Debouncer" section below.

See the headers in `include/basin/node_core/` for exact member names and
behaviour.

## Build / Install

From the workspace root:

```bash
colcon build --packages-select node_core
source install/setup.bash
```

Because `node_core` installs an include directory and a library, other
packages can add it as a dependency and link against it in CMake.

CMake usage in a consuming package:

```cmake
find_package(node_core REQUIRED)

add_executable(my_node src/my_node.cpp)
target_include_directories(my_node PRIVATE $<TARGET_PROPERTY:node_core,INTERFACE_INCLUDE_DIRECTORIES>)
target_link_libraries(my_node PRIVATE node_core)
ament_target_dependencies(my_node rclcpp rclcpp_components)
```

Or, when building a component/library, link `node_core` the same way and
use the exported headers from `include/`.

## Example: subclassing NodeCore

```cpp
#include <basin/node_core/node_core.hpp>

class MyNode : public basin::node_core::NodeCore {
public:
	MyNode() : NodeCore("my_node") {
		// constructor
	}

	void onTick() override {
		// periodic work
	}

	void shutdown() override {
		// cleanup before exit
	}
};
```

When using the `HeartbeatSender` directly:

```cpp
auto hb = std::make_unique<basin::monitor_core::HeartbeatSender>(this);
hb->startBeatTimer();
// or hb->sendBeat(basin::monitor_core::ProcessState::RUNNING);
```

## Dependencies

- `rclcpp`
- `rclcpp_components`
- `rcl_interfaces`
- `process_msgs`
- `status_msgs`
- `nlohmann_json` (determinism `StateArchive`)
- `rosbag2_cpp`, `rosbag2_storage` (determinism `ReplayHarness`)

Dependencies are declared in `package.xml` and are available at build-time
via `ament_cmake`.

---

## ParamClient — ROS2 Parameter Setting Client

### Overview

The `ParamClient` class provides a blocking/synchronous API for setting parameters on remote ROS2 nodes. This is useful for configuration management, runtime parameter updates, and testing scenarios.

### Features

- **Blocking/Synchronous API**: All operations wait for completion before returning
- **Node Availability Check**: Verify node is online before attempting parameter changes
- **Single or Multiple Parameters**: Set one parameter or multiple parameters in a single call
- **Error Handling**: Clear error messages when operations fail
- **Service Caching**: Reuses service clients for efficiency
- **Configurable Timeout**: Set custom timeouts for service calls

### Basic Usage

#### Include the Header

```cpp
#include "basin/node_core/param_client.hpp"
```

#### Create a Parameter Client

```cpp
auto node = std::make_shared<rclcpp::Node>("my_node");

// Create with default 5-second timeout
basin::node_core::ParamClient param_client(node);

// Or with custom timeout (in milliseconds)
basin::node_core::ParamClient param_client(node, 10000); // 10 seconds
```

#### Check if Node is Available

```cpp
// Check if target node is online before setting parameters
if (param_client.isNodeAvailable("/robot_controller")) {
    RCLCPP_INFO(node->get_logger(), "Node is available!");
} else {
    RCLCPP_ERROR(node->get_logger(), "Node not available: %s", 
                 param_client.getLastError().c_str());
}

// With custom timeout for the check (1 second)
if (param_client.isNodeAvailable("/robot_controller", 1000)) {
    // Node is online, proceed with parameter updates
}
```

#### Set a Single Parameter

```cpp
// Set an integer parameter
rclcpp::Parameter param_int("max_speed", 100);
if (param_client.setParameter("/robot_controller", param_int)) {
    RCLCPP_INFO(node->get_logger(), "Parameter set successfully");
} else {
    RCLCPP_ERROR(node->get_logger(), "Failed: %s", param_client.getLastError().c_str());
}

// Set a double parameter
rclcpp::Parameter param_double("threshold", 0.95);
param_client.setParameter("/sensor_node", param_double);

// Set a boolean parameter
rclcpp::Parameter param_bool("enable_debug", true);
param_client.setParameter("/logger_node", param_bool);

// Set a string parameter
rclcpp::Parameter param_string("config_file", "/path/to/config.yaml");
param_client.setParameter("/config_manager", param_string);
```

#### Set Multiple Parameters

```cpp
std::vector<rclcpp::Parameter> params = {
    rclcpp::Parameter("param1", 42),
    rclcpp::Parameter("param2", "value"),
    rclcpp::Parameter("param3", true),
    rclcpp::Parameter("param4", 3.14)
};

if (param_client.setParameters("/target_node", params)) {
    RCLCPP_INFO(node->get_logger(), "All parameters set successfully");
} else {
    RCLCPP_ERROR(node->get_logger(), "Error: %s", param_client.getLastError().c_str());
}
```

### Complete Example

```cpp
#include <rclcpp/rclcpp.hpp>
#include "basin/node_core/param_client.hpp"

int main(int argc, char** argv) {
    rclcpp::init(argc, argv);
    auto node = std::make_shared<rclcpp::Node>("config_manager");
    
    basin::node_core::ParamClient param_client(node, 5000);
    
    // Check if target node is available
    if (!param_client.isNodeAvailable("/robot_controller")) {
        RCLCPP_ERROR(node->get_logger(), "Target node not available");
        return 1;
    }
    
    // Set parameters
    std::vector<rclcpp::Parameter> params = {
        rclcpp::Parameter("max_speed", 100),
        rclcpp::Parameter("enable_safety", true)
    };
    
    if (param_client.setParameters("/robot_controller", params)) {
        RCLCPP_INFO(node->get_logger(), "Configuration updated successfully");
    }
    
    rclcpp::shutdown();
    return 0;
}
```

### API Reference

#### Constructor

```cpp
ParamClient(rclcpp::Node::SharedPtr node, int64_t timeout_ms = 5000)
```

- `node`: The ROS2 node that will own the service client
- `timeout_ms`: Default timeout for service calls in milliseconds (default: 5000ms)

#### Methods

**isNodeAvailable**
```cpp
bool isNodeAvailable(const std::string& target_node_name, int64_t timeout_ms = -1)
```
Check if a node is online and its parameter service is available. Use -1 for timeout_ms to use the default timeout.

**setParameter**
```cpp
bool setParameter(const std::string& target_node_name, const rclcpp::Parameter& param)
```
Set a single parameter on a target node.

**setParameters**
```cpp
bool setParameters(const std::string& target_node_name, const std::vector<rclcpp::Parameter>& params)
```
Set multiple parameters on a target node.

**getLastError**
```cpp
std::string getLastError() const
```
Get the last error message.

### Common Use Cases

#### Configuration Manager

```cpp
class ConfigManager {
    basin::node_core::ParamClient param_client_;
    
public:
    bool updateRobotConfig(const std::string& node_name, const RobotConfig& config) {
        // Check availability first
        if (!param_client_.isNodeAvailable(node_name)) {
            return false;
        }
        
        std::vector<rclcpp::Parameter> params = {
            rclcpp::Parameter("max_speed", config.max_speed),
            rclcpp::Parameter("acceleration", config.acceleration),
            rclcpp::Parameter("enable_safety", config.enable_safety)
        };
        return param_client_.setParameters(node_name, params);
    }
};
```

#### Runtime Tuning

```cpp
// Dynamically adjust parameters based on conditions
if (param_client.isNodeAvailable("/processing_node")) {
    if (system_load > 0.8) {
        param_client.setParameter("/processing_node", 
                                 rclcpp::Parameter("thread_count", 2));
    } else {
        param_client.setParameter("/processing_node", 
                                 rclcpp::Parameter("thread_count", 4));
    }
}
```

### Notes

- Always check `isNodeAvailable()` before setting parameters to avoid timeouts
- The target node must have its parameter service running
- Parameters must be declared on the target node before they can be set
- Some nodes may have read-only parameters that cannot be changed
- The API respects parameter validation rules defined in the target node

---

## Debouncer

`basin::node_core::Debouncer<T>` (`include/basin/node_core/debouncer.hpp`) is a
header-only, time-stable debouncer for a discrete signal — a boolean limit
switch, a mode enum, a small integer state code. A newly observed value only
becomes the committed output once it has persisted for a configured settle
window; any disagreeing sample before then restarts the window, and a sample
that returns to the current committed value clears the pending change
immediately.

The settle window can differ per edge. Pass a single duration for a symmetric
window, or a `(rising, falling)` pair to require different dwell times for a
rising edge (candidate is truthy — activation) versus a falling edge (candidate
is falsy — deactivation) — the usual need for limit switches and enable flags,
where engagement should be trusted quickly but release slowly (or vice versa).
For full control, pass a selector that maps a candidate value to its window.

Time is supplied by the caller rather than read from a clock, so `Debouncer`
carries no `rclcpp::Node`/clock dependency and stays usable under deterministic
replay and in unit tests. Feed timestamps from a single monotonic source; a
backwards jump restarts the settle window from the new `now`.

### API

```cpp
using SettleSelector = std::function<rclcpp::Duration(const T& candidate)>;

Debouncer(T initial, rclcpp::Duration settle);                                    // symmetric
Debouncer(T initial, rclcpp::Duration rising_settle, rclcpp::Duration falling_settle);
Debouncer(T initial, SettleSelector settle_for);                                  // per-value

bool update(const T& sample, const rclcpp::Time& now);  // true iff committed value changed
const T& value() const;                                 // current debounced value
bool pending() const;                                    // a change is waiting out the window
std::optional<T> pendingValue() const;                   // the candidate, if any
rclcpp::Duration settleDurationFor(const T& candidate) const;
void setSettleDuration(rclcpp::Duration settle);
void setSettleDuration(rclcpp::Duration rising_settle, rclcpp::Duration falling_settle);
void setSettleSelector(SettleSelector settle_for);
void reset(T value);                                      // force value, drop pending candidate
```

Reconfiguring the window (any `setSettle*` call) applies to an in-flight
candidate too. The `(rising, falling)` constructor and its `setSettleDuration`
overload additionally require `T` to be contextually convertible to `bool`.

### Usage

```cpp
#include <basin/node_core/debouncer.hpp>

// Trust contact after 20 ms, but wait 200 ms of clear signal before believing
// the boom has left the stow.
basin::node_core::Debouncer<bool> stowed_{
    false, rclcpp::Duration::from_seconds(0.02), rclcpp::Duration::from_seconds(0.2)};

void onLimitSwitch(const std_msgs::msg::Bool& msg) {
    if (stowed_.update(msg.data, now())) {
        RCLCPP_INFO(get_logger(), "boom stowed state -> %s", stowed_.value() ? "true" : "false");
    }
}
```

`T` defaults to `bool` but any equality-comparable, copyable type works
(`Debouncer<DriveMode>`, `Debouncer<int>`, ...); pass a `SettleSelector` for a
per-value window on types where "rising"/"falling" doesn't apply.

---

## Deterministic execution & replay (Phase 1)

Implements single-node determinism, state checkpointing, log-driven replay. Nodes inherit from `DeterministicNode` subclass of `NodeCore`. Lives under `include/basin/node_core/determinism/` and `basin::node_core::determinism`:

Running a deterministic node publishes executor dispatch records, publish provenance, and state snapshots as ordinary messages (`process_msgs/msg/DispatchLogEntry`, `PublishProvenance`, `StateSnapshot`) on per-node topics that `DeterministicNode` creates and owns (`~/determinism/dispatch_log`, `~/determinism/publish_log`, `~/determinism/state_snapshot`). If nothing is listening, the messages are simply dropped, same as any other unread topic.

Replay reads the state, publish records and executor dispatch back out of that bag to trigger the node in the same way.

- `DeterministicNode : public NodeCore`: A node that wants to participate in recording/replay derives from this instead of `NodeCore` directly. It creates a dedicated snapshot timer, the same way `NodeCore` always creates `tick_timer_`: its period comes from the node's `core.state_period` parameter, **defaulting to 20 seconds**. Every time it fires it calls `ExecutionRecorder::snapshotNow` directly. Set `core.state_period` to `0` to disable it for a node type with no meaningful state to checkpoint. A node that wants a snapshot on some other schedule instead can call `getRecorder()->snapshotNow(...)` directly (e.g. from `onTick()`).

- `ExecutionRecorder` — assigns sequence numbers for one node and publishes through the publishers `DeterministicNode` created for it. `snapshotNow(...)` publishes a state snapshot unconditionally — what the automatic timer calls, and safe to call directly too.

- `DeterministicExecutor` — a `SingleThreadedExecutor` that publishes `{sequence, entity_kind,entity_name, sim_time}` for every callback dispatch, in the executor's real order, before running it.

- `StateArchive` — thin `nlohmann::json` wrapper that should be used to store any internal state. This is what a `StateSnapshot` message's `state_json` field holds.

- `DeterministicNode::saveState(StateArchive&)` / `restoreState(const StateArchive&)` — protected and pure virtual: every concrete `DeterministicNode` subclass must implement both (an empty body is fine for a node with no meaningful state), so a node type can't silently end up unreplayable by forgetting to override them.

- `DeterministicNode::createDeterministicPublisher<T>(topic, qos)` — use in place of `create_publisher<T>(...)` for any publisher whose output affects node behavior.

- `DeterministicNode::createDeterministicSubscription<T>(topic, qos, callback)` — use in place of `create_subscription<T>(...)` for any subscription whose callback affects node behavior. Unlike the publisher case, this doesn't need to wrap anything for *recording* — `DeterministicExecutor` already records every dispatch, subscriptions included, by topic name, regardless of how the subscription was created. What it buys you: the topic is tracked in `getReplayTopics()` (so the `ros2 bag record` invocation covering everything a replay needs doesn't have to be hand-maintained separately), and the callback's normalized to `void(const T&)`, which is exactly the shape `ReplayHarness::subscriptionHandler<T>` needs to drive it during replay.

- `ReplayHarness` — reads a rosbag2 recording of a node's `determinism/*` topics: finds the nearest snapshot at or before a target sequence, decodes its `StateArchive`, and walks the dispatch log forward, invoking a caller-supplied `DispatchTable` per entry. `subscriptionHandler<T>(topic, callback)` builds one `DispatchTable` entry automatically: it reads every `T` recorded on `topic` from the same bag, in order, and feeds them to `callback` one per dispatch — i.e. it assumes the Nth dispatch of `topic` lines up with the Nth message recorded on `topic`, which holds as long as the bag actually captured that topic without dropping anything (see `getReplayTopics()` above).

### Usage

```cpp
class HexController : public basin::node_core::determinism::DeterministicNode { ... };

// Recording is on by default from construction -- deriving from DeterministicNode is the
// opt-in, there's no separate step to remember. getRecorder() returns the recorder the node
// created and already wired its determinism/* publishers into.
auto node = std::make_shared<HexController>(options);

basin::node_core::determinism::DeterministicExecutor executor(node->getRecorder());
executor.add_node(node);
executor.setClock(node->get_clock());
executor.spin();
```

Subscriptions whose callbacks affect node behavior go through `createDeterministicSubscription` instead of `create_subscription` — same call site, just a normalized `void(const T&)` callback:

```cpp
setpoint_sub_ = createDeterministicSubscription<std_msgs::msg::Float64>(
    "/boom_joint/setpoint", rclcpp::SystemDefaultsQoS(),
    [this](const std_msgs::msg::Float64& msg) { setpoint_ = msg.data; });
```

Run alongside a `ros2 bag record` covering `/hex_controller/determinism/dispatch_log`, `/hex_controller/determinism/publish_log`, `/hex_controller/determinism/state_snapshot`, and every topic in `node->getReplayTopics()` — that list is exactly "the normal topics the bag needs for subscription payload replay" from `createDeterministicSubscription`'s doc comment, so it doesn't have to be hand-maintained separately. Recording mode with nothing listening produces no artifact, by design.

Later, replay reads that bag back:

```cpp
basin::node_core::determinism::ReplayHarness harness("/path/to/bag", "/hex_controller");
auto snapshot = harness.findNearestSnapshot(target_sequence);
auto node = std::make_shared<HexController>(replay_options);
node->applySnapshot(basin::node_core::determinism::ReplayHarness::toStateArchive(*snapshot));
node->get_clock()->set_ros_time_override(snapshot->sim_time);

basin::node_core::determinism::DispatchTable table;
table["tick_timer"] = [&](auto&) { node->onTick(); };
// subscriptionHandler<T> reads every T recorded on this topic from the same bag and feeds them
// to the callback in order, one per dispatch -- no manual bag lookup to write.
table["/hex_controller/boom_joint/setpoint"] = harness.subscriptionHandler<std_msgs::msg::Float64>(
    "/hex_controller/boom_joint/setpoint", [&](const std_msgs::msg::Float64& msg) { node->onSetpoint(msg); });

harness.replayForward(*snapshot, target_sequence, table,
                       [&](rclcpp::Time t) { node->get_clock()->set_ros_time_override(t); });
```

A subclass implements checkpointing the same way it already implements `onTick`/`shutdown` — both are required:

```cpp
protected:
  void saveState(basin::node_core::determinism::StateArchive& archive) override {
    archive.put("integral_error", integral_error_);
  }
  void restoreState(const basin::node_core::determinism::StateArchive& archive) override {
    integral_error_ = archive.getOr<double>("integral_error", 0.0);
  }
```

### Deferred (not implemented here)
- **Multi-node/distributed replay** — explicitly out of scope for Phase 1.

---

## Maintainer & License

Maintainer: `coalman321 <cjtucker321@gmail.com>` (see `package.xml`).

License: `MIT` (see the repository root `LICENSE`)
