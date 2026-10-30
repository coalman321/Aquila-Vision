# log_util
This package contains the core library components for continuous ROS2 bag recording with snapshot management.

## Overview

This library provides reusable components that can be integrated into custom logging applications:

- **LogWriter**: Handles writing MCAP files to disk with time/size-based splitting
- **SnapshotWriter**: Manages snapshot metadata in YAML files
- **SnapshotTracker**: Runs manual and event snapshots through their lifetime, keeping a WAL so a crash never leaves one open
- **LogManager**: Manages the log directory, disk usage, and log rotation
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

Prepares the log directory, names each run's log, and keeps the disk in check: once usage reaches the rotation threshold it deletes the oldest MCAP files until usage is back under it. It never deletes a file that overlaps a snapshot (running or complete) or a pending upload manifest, nor the file being written, and it reports how much of the disk those protected files fill.

**Key Methods:**
- `set_log_directory()`: Sets the log directory to manage
- `set_disk_limits()`: Sets the rotation threshold and the snapshot usage threshold
- `prepare_log_directory()`: Creates the log directory, or throws if the path isn't one
- `next_log_name()`: Name for this run's log, one past the highest run number on disk
- `scan_log_directory()`: Scans for MCAP files and snapshots
- `check_disk()`: Rescans, rotates, and returns a `DiskCheckResult` (files rotated, usage, protected bytes, whether snapshot usage is at its threshold)
- `get_last_disk_check()`: The most recent `check_disk()` result
- `get_log_files()`: Gets list of all MCAP files
- `get_snapshot_log_files()`: Gets MCAP files for a specific snapshot
- `get_disk_usage_percent()`: Gets current disk usage percentage

**Thread Safety:** All methods are thread-safe using internal mutex.

**Example:**
```cpp
#include "log_util/log_manager.hpp"

log_util::LogManager manager;
manager.set_log_directory("/path/to/logs");
manager.set_disk_limits(80.0, 50.0); // rotate at 80%, flag snapshots filling 50%
manager.prepare_log_directory();
std::string log_name = manager.next_log_name("log_");

// Periodically: rotate if needed, never touching the file being written
log_util::DiskCheckResult disk = manager.check_disk(writer.get_current_file_path());
if (disk.rotated_files > 0) {
    std::cout << "Rotated " << disk.rotated_files << " old logs" << std::endl;
}
if (disk.snapshot_usage_high) {
    std::cout << "Snapshots fill " << disk.snapshot_usage_percent << "% of the disk" << std::endl;
}
```

### SnapshotExtractor

Extracts specific time ranges from MCAP logs into separate rosbag directories.

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
