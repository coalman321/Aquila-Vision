# log_util

> **Bazel:** this package is built with Bazel in this monorepo. The `colcon`/`ros2 run`/`ros2 launch` commands and `package.xml` snippets below are from the original colcon build; see the [Quick Start](../README.md#quick-start) for the Bazel equivalents.

This package contains the core library components for continuous ROS2 bag recording with snapshot management.

[![ROS2](https://img.shields.io/badge/ROS2-Humble-blue)](https://docs.ros.org/en/humble/)

## Overview

This library provides reusable components that can be integrated into custom logging applications:

- **LogWriter**: Handles writing MCAP files to disk with time/size-based splitting
- **SnapshotWriter**: Manages snapshot metadata in YAML files
- **LogManager**: Manages log storage, disk usage, and log rotation
- **SnapshotExtractor**: Extracts snapshot data from MCAP logs

## Package Split

This package was split from the original `continuous_logger` package to separate:
- **log_util** (this package): Core library functionality that can be reused
- **continuous_logger**: ROS node and CLI tools that use this library

This separation allows you to:
- Build custom logging applications using the library components
- Reuse the logging logic in different contexts
- Reduce dependencies for applications that only need specific components

## Dependencies

- `rclcpp`: ROS2 C++ client library
- `logging_msgs`: Custom message definitions for logging
- `rosbag2_cpp`: ROS2 bag recording API
- `rosbag2_storage`: ROS2 bag storage interface
- `yaml-cpp`: YAML file parsing and writing

## Installation

```bash
# Build the library package
cd ~/ros2_ws
colcon build --packages-select log_util
source install/setup.bash
```

## Usage

### As a Dependency

Add this package as a dependency in your `package.xml`:
```xml
<depend>log_util</depend>
```

Include the headers in your code:
```cpp
#include "log_util/log_writer.hpp"
#include "log_util/snapshot_writer.hpp"
#include "log_util/log_manager.hpp"
#include "log_util/snapshot_extractor.hpp"
```

Link against the library in `CMakeLists.txt`:
```cmake
find_package(log_util REQUIRED)

add_executable(your_app src/your_app.cpp)
ament_target_dependencies(your_app 
  log_util
  rclcpp
  # ... other dependencies
)
```

## API Reference

All classes are in the `basin::log_util` namespace.

### LogWriter

Writes messages and metadata to MCAP files with automatic file splitting.

**Key Methods:**
- `open()`: Opens a new log file for writing
- `close()`: Closes the current log file
- `write()`: Writes a serialized message to the log
- `write_metadata_event()`: Writes metadata events
- `create_topic()`: Creates a topic in the bag file
- `get_recording_directory()`: Gets the current recording path

**Thread Safety:** All methods are thread-safe using internal mutex.

**Example:**
```cpp
#include "log_util/log_writer.hpp"

log_util::LogWriter writer;

// Configure storage options
rosbag2_storage::StorageOptions storage_options;
storage_options.uri = "/path/to/logs/log_directory";
storage_options.storage_id = "mcap";
storage_options.max_bagfile_size = 100 * 1024 * 1024; // 100MB
storage_options.max_bagfile_duration = 60; // 60 seconds

writer.open(storage_options);

// Write messages
writer.write(serialized_msg, "/topic_name", "std_msgs/msg/String", timestamp);

writer.close();
```

### SnapshotWriter

Manages snapshot metadata including start/stop times, triggers, and tags.

**Key Methods:**
- `set_recording_directory()`: Sets where snapshots.yaml will be stored
- `start_snapshot()`: Starts a new snapshot
- `stop_snapshot()`: Stops an active snapshot
- `has_snapshot()`: Checks if a snapshot exists
- `is_snapshot_running()`: Checks if a snapshot is currently running
- `get_snapshot()`: Gets information about a specific snapshot
- `get_all_snapshots()`: Gets all snapshots
- `load_from_file()`: Loads snapshots from YAML
- `save_to_file()`: Saves snapshots to YAML

**Thread Safety:** All methods are thread-safe using internal mutex.

**Example:**
```cpp
#include "log_util/snapshot_writer.hpp"

log_util::SnapshotWriter writer;
writer.set_recording_directory("/path/to/logs");

// Start a snapshot
writer.start_snapshot(
    "collision_event_001",
    "event-collision",
    "Collision detected on front bumper",
    {"safety", "collision"}
);

// Stop the snapshot later
writer.stop_snapshot("collision_event_001");

// List all snapshots
auto snapshots = writer.get_all_snapshots();
for (const auto& snapshot : snapshots) {
    std::cout << "Snapshot: " << snapshot.id << std::endl;
}
```

### LogManager

Monitors disk usage, manages log files, and handles log rotation while protecting snapshot-associated files.

**Key Methods:**
- `set_log_directory()`: Sets the log directory to manage
- `set_disk_limits()`: Sets disk usage limits
- `scan_log_directory()`: Scans for MCAP files and snapshots
- `check_and_rotate_logs()`: Checks disk usage and rotates logs
- `get_log_files()`: Gets list of all MCAP files
- `get_snapshot_log_files()`: Gets MCAP files for a specific snapshot
- `get_disk_usage_percent()`: Gets current disk usage percentage
- `should_warn_disk_usage()`: Checks if warning threshold is exceeded

**Thread Safety:** All methods are thread-safe using internal mutex.

**Example:**
```cpp
#include "log_util/log_manager.hpp"

log_util::LogManager manager;
manager.set_log_directory("/path/to/logs");
manager.set_disk_limits(80.0, 70.0); // rotate at 80%, warn at 70%

// Scan existing logs
size_t num_logs = manager.scan_log_directory();

// Check and rotate if needed
if (manager.check_and_rotate_logs()) {
    std::cout << "Rotated old logs due to disk usage" << std::endl;
}

// Check disk usage
double usage = manager.get_disk_usage_percent();
if (manager.should_warn_disk_usage()) {
    std::cout << "Warning: High disk usage at " << usage << "%" << std::endl;
}
```

### SnapshotExtractor

Extracts specific time ranges from MCAP logs into separate rosbag directories.

**Key Methods:**
- `extract_snapshot()`: Extracts a single snapshot to a rosbag
- `extract_all_snapshots()`: Extracts all snapshots
- `list_snapshots()`: Lists all available snapshots
- `validate_log_directory()`: Validates log directory structure
- `find_relevant_mcap_files()`: Finds MCAP files for a time range

**Thread Safety:** Methods are not thread-safe; use external synchronization if needed.

**Example:**
```cpp
#include "log_util/snapshot_extractor.hpp"

log_util::SnapshotExtractor extractor;

// List snapshots
auto snapshots = extractor.list_snapshots("/path/to/logs");
for (const auto& snapshot : snapshots) {
    std::cout << "Found snapshot: " << snapshot.id << std::endl;
}

// Extract a specific snapshot
bool success = extractor.extract_snapshot(
    "/path/to/logs",
    "collision_event_001",
    "/path/to/extracted"
);

// Extract all snapshots
int count = extractor.extract_all_snapshots(
    "/path/to/logs",
    "/path/to/extracted"
);
std::cout << "Extracted " << count << " snapshots" << std::endl;
```

## Data Structures

### SnapshotInfo

```cpp
struct SnapshotInfo {
    std::string id;                    // Unique snapshot identifier
    int64_t start_time;                // Start time (epoch seconds)
    int64_t end_time;                  // End time (epoch seconds)
    std::string trigger;               // Trigger type (e.g., "manual", "event-collision")
    std::string description;           // Human-readable description
    std::vector<std::string> tags;     // Categorization tags
};
```

## File Formats

### snapshots.yaml

Snapshots are stored in YAML format:

```yaml
version: 1
snapshots:
  - id: "collision_event_001"
    start_time: 1696540800
    end_time: 1696540860
    trigger: "event-collision"
    description: "Collision detected on front bumper"
    tags:
      - "safety"
      - "collision"
```

## License

MIT - see the repository root `LICENSE`.

## Maintainer

Cole Tucker <cjtucker321@gmail.com>
