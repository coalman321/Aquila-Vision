# Production Logging

A comprehensive ROS2 logging ecosystem for production robotics with continuous logging, snapshot protection, and advanced data management tools.

## Overview

The Production Logging package provides a complete suite of tools for production robotics logging, including continuous data logging with intelligent disk management, snapshot protection, and event-triggered data capture. The system is designed with modularity in mind, separating core library functionality from application-specific implementations.

## Package Structure

Vendored into this monorepo from the standalone
[production-logging](https://gitlab.tuckers-workshop.xyz/robotics/basin/production-logging)
repo, which is now **superseded for development** — make changes here. These
are Bazel-only (no `package.xml`/`CMakeLists.txt`, so not colcon-buildable),
except `ros2prod_log_cli`, see below.

### Core Packages

- **[continuous_logger](continuous_logger/README.md)** (`//basin/logging/continuous_logger`) - Main ROS2 node and CLI tools for continuous logging
- **[log_util](log_util/README.md)** (`//basin/logging/log_util`) - Core library with reusable logging components
- **[log_util_py](log_util_py/README.md)** (`//basin/logging/log_util_py`) - Python bindings and tools for the logging library
- **[pcap_recorder](pcap_recorder/README.md)** (`//basin/logging/pcap_recorder`) - Keeps a rolling in-memory tcpdump capture and publishes it when a fault is raised
- **[logging_msgs](../../interfaces/logging_msgs/)** - Custom ROS2 message definitions for logging (lives under `//interfaces`, not here)
- **[ros2prod_log_cli](ros2prod_log_cli/)** - `ros2 bag` verbs for production log management. **Not built by Bazel** (see below)

### Key Features

- **Continuous Data Logging** - Automatic, reliable data capture for production robotics
- **Intelligent Disk Management** - Prevents disk overflow with smart log rotation
- **Snapshot Protection** - Critical data is never deleted automatically
- **Event-Triggered Snapshots** - Automatic data capture around important events
- **Modular Architecture** - Reusable components for custom logging applications
- **MCAP Format Support** - Modern, efficient storage format
- **Topic Discovery & Throttling** - Automatic topic detection with configurable data rates
- **CLI Tools** - Comprehensive command-line utilities for log management

## Quick Start

```bash
# Build and test everything in this directory
bazel build //basin/logging/...
bazel test //basin/logging/...

# Run the logger (standalone process) with the shipped example config
bazel run //basin/logging/continuous_logger:logger_node -- \
    --ros-args --params-file basin/logging/continuous_logger/config/logger.yaml

# Inspect snapshots in a log directory
bazel run //basin/logging/continuous_logger:snapshot_extractor_cli -- list /tmp/log
bazel run //basin/logging/continuous_logger:snapshot_extractor_cli -- extract /tmp/log snapshot_id /tmp/extracted

# Python bindings and the example scripts
bazel run //basin/logging/log_util_py:query_snapshots -- list /tmp/log
```

The `ros2 run ...`/`ros2 launch ...`/`colcon build ...` commands in the
package READMEs below come from the colcon original; the mapping is
`ros2 run continuous_logger snapshot_extractor_cli` -> `bazel run
//basin/logging/continuous_logger:snapshot_extractor_cli --`. The node's
executable is `logger_node`, as the launch files use, not `logger`. There is
no Bazel equivalent of `ros2 launch` for it (see below).

## Bazel notes

- **`bazel run` needs `RUNFILES_DIR`, which `.bazelrc` provides.** rclcpp's
  generic subscriptions (which the logger uses to record arbitrary topics)
  resolve message typesupport through the Bazel runfiles library, and `bazel
  run` unsets `RUNFILES_DIR`, so without the `--run_env=RUNFILES_DIR=..` line in
  [.bazelrc](../../.bazelrc) the node dies on its first discovery tick (one
  second in) with `Failed to create Runfiles`. Running the built binary by hand
  needs `RUNFILES_DIR` pointing at `logger_node.runfiles`.
- **The launch files have no Bazel target.** They ship in the `continuous_logger`
  ament share directory, but `//tools/ros2_launch` can't host a child that
  opens rosbag2 (the launcher's own ament prefix lacks the storage plugins;
  mirroring them in pulls rosbag2's test message libraries into its `lib/`,
  which its Python startup hook then fails to import). `logger_launch` targets
  were tried and removed.
- **Deployed as a component.** `LoggerNode` is a component, built as
  `:logger_component` and registered under the `continuous_logger` ament
  package (`:ament_index`, which also publishes `config/` and `launch/`), so a
  `//basin/core/node_core:basin_container` config loads it with
  `library_name: basin::continuous_logger::LoggerNode` /
  `package_name: continuous_logger`. `:logger_component_test` does exactly
  that. `:logger_node` is the same node as a standalone process, from
  `src/logger_main.cpp`.
- **MCAP needs the storage plugins at runtime.** `log_util` links
  only the abstract rosbag2 API; the standalone binaries and the Python module
  add `rosbag2_storage_default_plugins` to their `dynamic_deps` and the rosbag2
  `ament_index` data (see the `STORAGE_PLUGIN_DEPENDENCIES` comment in
  [continuous_logger/BUILD.bazel](continuous_logger/BUILD.bazel)); the
  container already carries them. Forgetting this fails at *runtime* with
  `package 'rosbag2_storage' not found`, not at link time.
- **The Python module preloads the RMW implementation.** `librcl` runs its RMW
  check as a static initializer, and under this toolchain that nested `dlopen`
  aborts the interpreter during `import`; `log_util_py/__init__.py`
  loads `lib$RMW_IMPLEMENTATION.so` first. It also has to be a
  `cc_shared_library` (RCR's own `rclpy` extension is built the same way) with
  the shared libraries listed in the `py_library`'s `data`.
- **`ros2prod_log_cli` is not built.** It is a `ros2bag` verb extension
  (`ament_python` entry points) and RCR's `ros2bag` module exposes no Python
  library target to depend on, so it is kept as vendored source only.
- **`logger_and_uploader.launch.py` was dropped.** It launched a
  `snapshot_uploader` package that is not part of this repo.
- Two fixes to the vendored sources: `LogInfo::operator<` in `logger_node.cpp`
  is now `const` (libc++'s `std::sort` needs it), and the binding for
  `find_relevant_mcap_files` returns `list[str]` as its README documents
  (it returned `std::filesystem::path`, which pybind11 can't convert, so every
  call raised `TypeError`).

## Package Documentation

For detailed documentation, configuration options, and usage examples, see the README for each package:

- **[Continuous Logger](continuous_logger/README.md)** - Complete documentation for the logger node, snapshot extraction, event-triggered logging, and CLI tools
- **[Continuous Logger Library](log_util/README.md)** - Core library API documentation, component architecture, and integration guide
- **[Continuous Logger Python](log_util_py/README.md)** - Python bindings, utilities, and examples

## Architecture

The logging system is built with a modular architecture:

```
Production Logging Ecosystem:
  
  continuous_logger (Application Layer)
  ├── LoggerNode - Main ROS2 orchestrator
  ├── snapshot_extractor_cli - CLI extraction tool
  └── Launch files & configurations
  
  log_util (Core Library)
  ├── LogWriter - MCAP file writing
  ├── SnapshotWriter - Snapshot metadata management  
  ├── LogManager - Disk management & rotation
  └── SnapshotExtractor - Data extraction logic
  
  log_util_py (Python Interface)
  ├── Python bindings for core library
  ├── Analysis tools
  └── Utilities
  
  Supporting Packages:
  ├── logging_msgs - Message definitions
  └── ros2prod_log_cli - CLI management tools
```

This separation allows:
- Using core logging functionality in custom applications
- Building specialized logging tools with the library components
- Python integration for data analysis and processing
- Command-line management of production logs

## Use Cases

### Production Robotics
- Continuous data recording during robot operation
- Automatic snapshot capture around critical events (errors, collisions)
- Intelligent disk management for long-running systems
- Post-incident data analysis and debugging

### Development & Testing  
- Automated test data capture with event markers
- Replay specific scenarios using extracted snapshots  
- Performance analysis with throttled high-frequency topics
- Integration testing with configurable logging setups

### Data Analysis
- Extract specific time windows for offline analysis
- Query metadata events and system state changes
- Convert logs to standard rosbag format for toolchain compatibility
- Archive important events separately from bulk data


## Dependencies

- `rclcpp` - ROS2 C++ client library
- `rclcpp_components` - Component architecture
- `rosbag2_cpp` - ROS2 bag recording API
- `rosbag2_storage` - Storage interfaces
- `yaml-cpp` - YAML file handling
- `basin/core/node_core` - node base class and parameter handling (`//basin/core/node_core`)

See individual package documentation for complete dependency lists.
