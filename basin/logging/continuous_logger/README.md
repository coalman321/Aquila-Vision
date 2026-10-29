# Continuous Logger

> **Bazel:** this package is built with Bazel in this monorepo. The `colcon`/`ros2 run`/`ros2 launch` commands and `package.xml` snippets below are from the original colcon build; see the [Quick Start](../README.md#quick-start) for the Bazel equivalents.

A ROS2 node and CLI tools for continuous logging with automatic disk management, snapshot protection, and event-triggered data capture.

[![ROS2](https://img.shields.io/badge/ROS2-Humble-blue)](https://docs.ros.org/en/humble/)

## Table of Contents

- [Overview](#overview)
- [Package Structure](#package-structure)
- [Key Features](#key-features)
- [Architecture](#architecture)
- [Quick Start](#quick-start)
- [Configuration](#configuration)
- [Usage Guide](#usage-guide)
  - [Manual Snapshots](#manual-snapshots)
  - [Event-Triggered Snapshots](#event-triggered-snapshots)
  - [Metadata Events](#metadata-events)
  - [Disk Management](#disk-management)
  - [Status Topic](#status-topic)
  - [Snapshot Extraction](#snapshot-extraction)
- [Event Snapshot Naming](#event-snapshot-naming)
- [File Structure](#file-structure)
- [Quick Reference](#quick-reference)
- [Implementation Details](#implementation-details)
- [Troubleshooting](#troubleshooting)
- [Future Enhancements](#future-enhancements)

---

## Overview

The Continuous Logger package provides a ROS2 node and CLI tools for production robotics logging. It provides continuous data logging with intelligent disk management, snapshot protection, and automatic event-based data capture.

The implementation uses the `log_util` package for core functionality.

## Package Structure

This package is split into two components:

- **log_util**: Core library with reusable components (LogWriter, SnapshotWriter, LogManager, SnapshotExtractor)
- **continuous_logger** (this package): ROS node and CLI tools that use the library

**What's in this package:**
- `logger` - ROS2 node for continuous logging
- `snapshot_extractor_cli` - Command-line tool to extract snapshots from logs

**What's in the library package:**
- Core logging functionality that can be reused in other applications
- See `log_util` package documentation for library API details

This separation allows:
- Building custom logging applications using the library components
- Using the logger node without understanding internal implementation
- Extracting snapshots from logs using the CLI tool
- Reusing logging logic in different contexts

### Dependencies

- `log_util` - Core library components
- `rclcpp` - ROS2 C++ client library
- `rclcpp_components` - ROS2 component support
- `logging_msgs` - Custom message definitions for logging
- `rosbag2_cpp` - ROS2 bag recording API
- `rosbag2_storage` - ROS2 bag storage interface

### Key Features

1. **Modular Architecture** - Each component has a single, well-defined responsibility
2. **Thread Safety** - All components use mutex locks for safe concurrent access
3. **Automatic Disk Management** - Prevents disk overflow with intelligent log rotation
4. **Snapshot Protection** - Important data is never deleted automatically
5. **Flexible Configuration** - All parameters configurable via ROS parameters
6. **Topic Throttling** - Reduce disk usage for high-frequency topics
7. **Discovery System** - Automatically finds and subscribes to topics
8. **MCAP Format** - Modern, efficient storage format
9. **Event-Triggered Snapshots** - Automatic data capture around critical events with configurable time windows
10. **Per-Event Counters** - Unique snapshot numbering for each event type with persistence across restarts
11. **Snapshot Extraction Tool** - CLI utility to extract snapshots from log files to standalone rosbags

---

## Architecture

This package depends on `log_util` which provides four main components:

### 1. Log Writer (from log_util)
Main log writer that handles writing MCAP files to disk.
- Splits files based on time or size
- Writes metadata events as a special channel (`/log/metadata`)
- Manages topic creation and serialization
- Thread-safe write operations

### 2. Snapshot Writer (from log_util)
Handles writing snapshot metadata to a YAML file in the log directory.
- Start/stop snapshot tracking
- Stores snapshot metadata (ID, start/end times, trigger, description, tags)
- Persists snapshots to `snapshots.yaml` file in the log directory
- Thread-safe operations
- Time override support for event-triggered snapshots

### 3. Log Manager (from log_util)
Manages log storage on disk.
- Monitors log directory for new MCAP files
- Monitors snapshot file for new snapshots
- Provides interface for querying snapshots
- Rotates old log files based on disk capacity
- Ensures snapshot-associated logs are preserved
- Provides disk usage warnings

### 4. Logger Node (this package)
The main ROS2 node that orchestrates all library components.
- Generic topic subscription with discovery
- Metadata event handling
- Snapshot service interface
- Periodic disk usage checking
- Automatic log file rotation
- Event-triggered snapshot support

### 5. Snapshot Extractor CLI (this package)
Command-line tool to extract snapshots from log directories.

### Component Diagram

```
continuous_logger package:
  LoggerNode
  ├── LogWriter (from lib)
  ├── SnapshotWriter (from lib)
  └── LogManager (from lib)
      └── SnapshotWriter (from lib)
  
  snapshot_extractor_cli
  └── SnapshotExtractor (from lib)
```

---

## Quick Start

### Installation & Setup

```bash
cd /home/coalman321/frc-2026
colcon build --packages-select continuous_logger
source install/setup.bash
```

### Starting the Logger

**Default Configuration:**
```bash
ros2 run continuous_logger logger
```

**With Config File:**
```bash
ros2 launch continuous_logger logger_with_config.launch.py
```

**With Custom Parameters:**
```bash
ros2 launch continuous_logger logger.launch.py \
  log_path:=/data/logs \
  log_split_time:=120 \
  rotation_threshold_percent:=85.0
```

Or with ROS args:
```bash
ros2 run continuous_logger logger --ros-args \
  -p log_path:=/data/logs \
  -p log_split_time:=60 \
  -p rotation_threshold_percent:=85.0
```

---

## Configuration

### Configuration Parameters

The logger node accepts the following parameters:

#### Topic Configuration
- `topics.<key>.name`: Topic name to subscribe to. A `*` matches one name segment, so
  `/*/driver/camera_info` logs `/front/driver/camera_info`, `/back/driver/camera_info` and so on. With a
  pattern configured the logger keeps looking for new topics, so ones that start later are logged too;
  with only plain names it stops once all of them are found.
- `topics.<key>.throttle`: Log 1 in every N+1 messages (0 = log all)

#### Log File Configuration
- `log_path`: Path to the log directory (default: `/tmp/log`)
- `log_prefix`: Prefix for log directory names (default: `log_`)
- `log_split_time`: Time between split files in seconds (default: 30)
- `log_split_size`: Max log size in bytes (default: max value)

#### Disk Management Configuration
- `rotation_threshold_percent`: Disk usage percentage at which the oldest logs no snapshot protects are deleted (default: 70.0)
- `disk_warning_threshold_percent`: Warning threshold percentage (default: 80.0)
- `disk_check_interval`: Interval for checking disk usage in seconds (default: 60.0)

#### Event Snapshot Configuration
- `event_snapshot_keys`: List of event snapshot identifiers
- `<event_key>.event_name`: Name of the event to trigger on
- `<event_key>.time_before`: Seconds before event to include in snapshot (default: 5.0)
- `<event_key>.time_after`: Seconds after event to include in snapshot (default: 5.0)
- `<event_key>.tags`: List of tags to apply to the snapshot
- `<event_key>.description`: Description template (use `{event_name}` as placeholder)

### Example Configuration File

Create `config/logger.yaml`:

```yaml
/**:
  ros__parameters:
    # Basic log configuration
    log_path: "/data/logs"
    log_prefix: "robot_"
    log_split_time: 120  # 2 minutes
    
    # Disk management
    rotation_threshold_percent: 85.0
    disk_warning_threshold_percent: 75.0
    disk_check_interval: 60.0
    
    # Topics to log
    topic_keys: ['odom', 'scan', 'cmd_vel', 'camera']
    
    odom:
      name: "/robot/odom"
      throttle: 0  # Log every message
    
    scan:
      name: "/robot/scan"
      throttle: 5  # Log every 5th message
    
    cmd_vel:
      name: "/robot/cmd_vel"
      throttle: 0
    
    camera:
      name: "/robot/camera/image"
      throttle: 30  # Log every 30th frame
    
    # Event snapshot configurations
    event_snapshot_keys: ['critical_error', 'collision', 'milestone']
    
    critical_error:
      event_name: "system_critical_error"
      time_before: 15.0
      time_after: 10.0
      tags: ["error", "critical", "automated"]
      description: "Critical error: {event_name}"
    
    collision:
      event_name: "collision_detected"
      time_before: 5.0
      time_after: 5.0
      tags: ["collision", "safety", "incident"]
      description: "Collision event captured automatically"
    
    milestone:
      event_name: "mission_milestone"
      time_before: 2.0
      time_after: 2.0
      tags: ["milestone", "success"]
      description: "Milestone achieved: {event_name}"
```

---

## Usage Guide

### Complete Workflow Example

Here's a complete example showing the full lifecycle: recording → snapshot creation → extraction → playback.

**1. Start the logger:**
```bash
ros2 run continuous_logger logger
```

**2. Create an event-triggered snapshot by publishing a timed event:**
```bash
ros2 topic pub /log/metadata logging_msgs/msg/LogMetadata \
  "{header: {stamp: {sec: $(date +%s), nanosec: 0}}, \
    timed_event: true, \
    key_name: 'test_event', \
    yaml_metadata: 'test: data'}" --once
```

**3. Wait for the logger to automatically create and finalize the snapshot**

**4. List the created snapshots:**
```bash
ros2 run continuous_logger snapshot_extractor_cli list /tmp/log
```

**5. Extract the snapshot:**
```bash
ros2 run continuous_logger snapshot_extractor_cli extract \
  /tmp/log event_test_event_0 /tmp/extracted
```

**6. Play the extracted snapshot:**
```bash
ros2 bag play /tmp/extracted/event_test_event_0
```

### Manual Snapshots

Manually controlled snapshots that you start and stop via service calls.

**Start a Snapshot:**
```bash
ros2 service call /log/snapshot logging_msgs/srv/SnapshotRequest \
  "{request_id: 1, snapshot_name: 'important_event'}"
```

**Stop a Snapshot:**
```bash
ros2 service call /log/snapshot logging_msgs/srv/SnapshotRequest \
  "{request_id: 2, snapshot_name: 'important_event'}"
```

**Ping/Check:**
```bash
ros2 service call /log/snapshot logging_msgs/srv/SnapshotRequest \
  "{request_id: 0, snapshot_name: ''}"
```

#### Service Response Codes

| Code | Name | Description |
|------|------|-------------|
| 0 | RESPONSE_OK | Success |
| 1 | RESPONSE_REQ_INVALID | Invalid request ID |
| 2 | RESPONSE_INVALID_NAME | Invalid snapshot name |
| 3 | RESPONSE_SNAPSHOT_ACTIVE | Snapshot already running |
| 4 | RESPONSE_SNAPSHOT_EXISTS | Snapshot already exists |

### Event-Triggered Snapshots

Event-triggered snapshots automatically capture data around important events with configurable time windows. This is especially useful for:
- Capturing context around errors or failures
- Recording collision events with pre-collision data
- Preserving goal achievement moments
- Documenting critical state changes

#### How It Works

1. **Configuration**: User configures event snapshot triggers in YAML
2. **Event Publication**: System publishes a timed LogMetadata event
3. **Event Detection**: LoggerNode receives event and checks configuration
4. **Snapshot Creation**: 
   - Generates unique ID: `event_<event_name>_<counter>`
   - Calculates start time: `event_time - time_before`
   - Calculates finalize time: `event_time + time_after`
   - Creates snapshot with calculated start time
   - Adds to pending list
5. **Snapshot Finalization**: Timer checks pending snapshots every second and finalizes those past their finalize time

#### Example Timeline

```
Event occurs at T=100s
time_before = 10s
time_after = 5s

T=90s  ─┐
        │ Snapshot captures this period
T=100s ─┼─ Event occurs
        │ Snapshot continues recording
T=105s ─┘ Snapshot finalized
```

#### Triggering Event Snapshots

Trigger by publishing timed event:

```bash
ros2 topic pub /log/metadata logging_msgs/msg/LogMetadata \
  "{header: {stamp: {sec: $(date +%s), nanosec: 0}}, \
    timed_event: true, \
    key_name: 'system_error', \
    yaml_metadata: 'details: error_info'}" --once
```

**What happens:**
1. Logger receives the timed event
2. Matches `key_name: 'system_error'` to config
3. Creates snapshot `event_system_error_0`
4. Sets start time to 10 seconds before the error
5. Records for 5 more seconds
6. Applies configured tags
7. Sets trigger: `event-system_error`

#### Python Example

```python
#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from logging_msgs.msg import LogMetadata

class EventPublisher(Node):
    def __init__(self):
        super().__init__('event_publisher')
        self.publisher = self.create_publisher(
            LogMetadata, 
            '/log/metadata', 
            10
        )
    
    def publish_critical_error(self, error_info):
        """Publish a critical error event that triggers snapshot"""
        msg = LogMetadata()
        
        # Set timestamp to current time
        now = self.get_clock().now()
        msg.header.stamp = now.to_msg()
        
        # This is a timed event that can trigger snapshots
        msg.timed_event = True
        
        # Event name matches configuration
        msg.key_name = 'system_critical_error'
        
        # YAML formatted metadata
        msg.yaml_metadata = f"""
error_code: {error_info['code']}
severity: {error_info['severity']}
module: {error_info['module']}
message: {error_info['message']}
"""
        
        self.publisher.publish(msg)
```

### Metadata Events

Metadata events are written to the `/log/metadata` topic as `logging_msgs/msg/LogMetadata` messages. These are stored in a special channel in the MCAP file.

**Timed Event:**
```bash
ros2 topic pub /log/metadata logging_msgs/msg/LogMetadata \
  "{header: {stamp: {sec: $(date +%s), nanosec: 0}}, \
    timed_event: true, \
    key_name: 'event_name', \
    yaml_metadata: 'key: value'}" --once
```

**General Metadata:**
```bash
ros2 topic pub /log/metadata logging_msgs/msg/LogMetadata \
  "{timed_event: false, \
    key_name: 'metadata_key', \
    yaml_metadata: 'data: value'}" --once
```

### Disk Management

The logger automatically manages disk space:

```yaml
rotation_threshold_percent: 80.0          # Start deleting old MCAP files at 80% disk usage
disk_warning_threshold_percent: 70.0   # Warn when disk usage exceeds 70%
disk_check_interval: 60.0             # Check disk every 60 seconds
```

**Important**: MCAP files that overlap with any snapshot's time range are never automatically deleted! Only MCAP files that don't overlap with snapshots are eligible for rotation.

The logger logs warnings when disk usage exceeds the warning threshold:

```
[WARN] [continuous_logger]: Disk usage is high: 72.5%
```

It also raises a fault for as long as usage is at or above the warning threshold: fault system
`continuous_logger`, name `disk_usage_high`, on `/state/fault_report` like any other node's (the
fault monitor TUI and the Foxglove Fault Monitor list it). The description gives the usage and both
thresholds. Each disk check (every `disk_check_interval`) reports it if usage is still at or above the
threshold once any rotation it did is done. It expires after two check intervals, so it stays up
through a late check and clears at most two intervals after a check finds usage back under the
threshold. Nothing acts on it unless the safety aggregator has a handler for it (see
`basin/safety/cfg/safety_agg.yaml`); without one it is only reported, which is what you want for a
full log disk, as it does not affect driving.

When the max threshold is reached, it automatically deletes the oldest MCAP files that are not associated with snapshots.

### Status Topic

Every tick (`core.tick_period`, 0.1 s by default) the logger publishes a
`logging_msgs/msg/LoggerStatus` on `/log/logger_status` (reliable, transient local, depth 1, so a
subscriber that joins late gets the latest one at once):

| Field | Meaning |
|-------|---------|
| `stamp` | When the status was built |
| `log_path`, `disk_usage_percent`, `disk_total_bytes`, `disk_free_bytes` | The filesystem the log directory is on. The percentage is the one rotation compares against `disk_rotation_threshold_percent` |
| `disk_rotation_threshold_percent`, `disk_warning_threshold_percent` | The configured limits, so a reader can tell how close rotation is |
| `events` | One entry per `event_snapshots.<key>` configuration: its name, window, tags, description and `trigger_count`, the number of snapshots it has triggered (counting earlier runs), which is also the `<n>` of its next `event_<name>_<n>` snapshot |
| `snapshots` | Every snapshot that is running (manual, not yet stopped) or pending (an event snapshot inside its `time_after` window), then the 50 most recently started completed ones, each group newest first. Times are epoch seconds, and `end_time` is -1 until the snapshot has finished |
| `total_snapshot_count` | How many snapshots exist in all, including those `snapshots` leaves out |

```bash
ros2 topic echo /log/logger_status
```

### Snapshot Extraction

The `snapshot_extractor_cli` tool allows you to extract snapshots from log files and create standalone rosbag directories. This is useful for:
- Sharing specific data segments with others
- Archiving important events separately
- Analyzing snapshots offline
- Reducing dataset size for testing

**Note:** The extraction logic is implemented in the `log_util` package, making it reusable in custom applications. This CLI tool provides a convenient command-line interface to that functionality.

#### Available Commands

**List all snapshots:**
```bash
ros2 run continuous_logger snapshot_extractor_cli list <log_dir>
```

**Extract a specific snapshot:**
```bash
ros2 run continuous_logger snapshot_extractor_cli extract <log_dir> <snapshot_id> <output_dir>
```

**Extract all snapshots:**
```bash
ros2 run continuous_logger snapshot_extractor_cli extract-all <log_dir> <output_dir>
```

**Validate log directory:**
```bash
ros2 run continuous_logger snapshot_extractor_cli validate <log_dir>
```

#### Usage Examples

**List snapshots in a log directory:**
```bash
ros2 run continuous_logger snapshot_extractor_cli list /data/logs

# Output:
# Found 3 snapshot(s) in: /data/logs
# 
# Snapshot 1:
#   ID: event_collision_detected_0
#   Start Time: 2025-10-05 14:30:15 (1728145815)
#   End Time: 2025-10-05 14:30:25 (1728145825)
#   Duration: 10 seconds
#   Trigger: event-collision_detected
#   Description: Collision event captured automatically
#   Tags: collision, safety, incident
```

**Extract a single snapshot:**
```bash
ros2 run continuous_logger snapshot_extractor_cli extract \
  /data/logs \
  event_collision_detected_0 \
  /data/extracted

# Creates: /data/extracted/event_collision_detected_0/
# Contains: 
#   - MCAP rosbag files with data from the snapshot time range
#   - snapshots.yaml with the snapshot metadata
#   - metadata.yaml (standard rosbag2 metadata)
```

**Extract all snapshots:**
```bash
ros2 run continuous_logger snapshot_extractor_cli extract-all \
  /data/logs \
  /data/extracted

# Creates separate directories for each snapshot:
#   /data/extracted/event_collision_detected_0/
#   /data/extracted/event_system_error_0/
#   /data/extracted/event_milestone_0/
```

**Validate before extraction:**
```bash
ros2 run continuous_logger snapshot_extractor_cli validate /data/logs

# Output:
# ✓ Log directory is valid: /data/logs
# ✓ Found 3 snapshot(s)
```

#### Playing Extracted Snapshots

Once extracted, you can play the snapshot like any standard rosbag:

```bash
# Play the extracted snapshot
ros2 bag play /data/extracted/event_collision_detected_0

# View snapshot info
ros2 bag info /data/extracted/event_collision_detected_0

# View snapshot metadata
cat /data/extracted/event_collision_detected_0/snapshots.yaml
```

#### Output Structure

Each extracted snapshot creates a directory containing:

```
event_collision_detected_0/
├── event_collision_detected_0_0.mcap    # Rosbag data
├── metadata.yaml                         # Rosbag2 metadata
└── snapshots.yaml                        # Snapshot metadata
```

The `snapshots.yaml` file contains:
```yaml
version: 1
snapshots:
  - id: "event_collision_detected_0"
    start_time: 1728145815
    end_time: 1728145825
    trigger: "event-collision_detected"
    description: "Collision event captured automatically"
    tags:
      - "collision"
      - "safety"
      - "incident"
```

---

## Event Snapshot Naming

### Naming Format

Event-triggered snapshots automatically generate unique IDs:

```
event_<event_name>_<counter>
```

Where:
- `event_` - Fixed prefix for all event-triggered snapshots
- `<event_name>` - The event name from your configuration
- `<counter>` - Per-event-name counter starting at 0

### Per-Event Counters

Each event type maintains its own independent counter:

| Event Name | First Occurrence | Second Occurrence | Third Occurrence |
|------------|------------------|-------------------|------------------|
| `system_error` | `event_system_error_0` | `event_system_error_1` | `event_system_error_2` |
| `collision_detected` | `event_collision_detected_0` | `event_collision_detected_1` | `event_collision_detected_2` |
| `goal_reached` | `event_goal_reached_0` | `event_goal_reached_1` | `event_goal_reached_2` |

### Counter Persistence

**Important:** Counters are initialized from existing snapshots in the `snapshots.yaml` file when the logger starts.

**How it works:**
1. When the logger node starts, it reads the existing `snapshots.yaml` file
2. It parses all snapshot IDs with format `event_<name>_<number>`
3. For each event name, it sets the counter to the highest number found + 1
4. New events will continue from where the previous session left off

**Example:**
```yaml
# Existing snapshots.yaml when logger starts
snapshots:
  - id: "event_system_error_0"
  - id: "event_system_error_1"
  - id: "event_collision_detected_0"

# Logger initializes counters:
# - system_error: 2 (next will be event_system_error_2)
# - collision_detected: 1 (next will be event_collision_detected_1)

# After logger restarts and new events occur:
- id: "event_system_error_2"      # Continues from 1
- id: "event_collision_detected_1" # Continues from 0
- id: "event_system_error_3"      # Next in sequence
```

**Benefits:**
- ✅ No duplicate snapshot IDs across restarts
- ✅ Counter sequences remain consistent
- ✅ Easy to see total number of events across all sessions
- ✅ Snapshot files remain valid after logger restarts

### Filtering Snapshots

To find all snapshots for a specific event:

```bash
# Using grep
grep "event_system_error_" /tmp/log/snapshots.yaml

# Count occurrences
grep -c "event_system_error_" /tmp/log/snapshots.yaml

# List all unique event names
grep "id:" /tmp/log/snapshots.yaml | \
  sed 's/.*event_\(.*\)_[0-9]*.*/\1/' | \
  sort -u
```

### Best Practices

1. **Use Descriptive Event Names**
   ```yaml
   # Good
   event_name: "navigation.path_planning_failed"
   event_name: "safety.collision_detected"
   
   # Avoid
   event_name: "e1"
   event_name: "thing"
   ```

2. **Use Hierarchical Naming**
   ```yaml
   event_name: "system.hardware.motor_failure"
   event_name: "system.hardware.sensor_failure"
   event_name: "safety.collision.front"
   ```

3. **Consistent Naming Convention**
   - Choose one: snake_case, dot.notation, or kebab-case
   - Apply consistently across all events

---

## File Structure

### Log Files

Logs are stored as individual MCAP files in a flat directory structure:

```
/data/logs/
  ├── log_0.mcap                # First log file
  ├── log_0_0.mcap              # First split of log_0
  ├── log_0_1.mcap              # Second split of log_0
  ├── log_1.mcap                # Second log session
  ├── log_1_0.mcap              # First split of log_1
  ├── metadata.yaml             # ROS bag metadata
  └── snapshots.yaml            # Snapshot metadata
```

**Note:** All MCAP files are stored in the same directory. The rosbag2 writer automatically handles file splitting and will create files like `log_0_0.mcap`, `log_0_1.mcap`, etc. when a split occurs.

### Snapshot File Format

The `snapshots.yaml` file in the log directory contains:

```yaml
version: 1
snapshots:
  - id: "event_system_critical_error_0"
    start_time: 1704067185    # Unix timestamp (seconds)
    end_time: 1704067210      # Unix timestamp (seconds)
    trigger: "event-system_critical_error"
    description: "Critical error: system_critical_error"
    tags:
      - "error"
      - "critical"
      - "automated"
  - id: "event_collision_detected_0"
    start_time: 1704067295
    end_time: 1704067305
    trigger: "event-collision_detected"
    description: "Collision event captured automatically"
    tags:
      - "collision"
      - "safety"
```

**Important:** The snapshots file protects individual MCAP files that overlap with the snapshot's time range. When disk rotation occurs, only MCAP files that don't overlap with any snapshot will be deleted.

---

## Quick Reference

### Common Commands

```bash
# Build
colcon build --packages-select continuous_logger
source install/setup.bash

# Run with defaults
ros2 run continuous_logger logger

# Run with config
ros2 launch continuous_logger logger_with_config.launch.py

# Start snapshot
ros2 service call /log/snapshot logging_msgs/srv/SnapshotRequest \
  "{request_id: 1, snapshot_name: 'event_name'}"

# Stop snapshot
ros2 service call /log/snapshot logging_msgs/srv/SnapshotRequest \
  "{request_id: 2, snapshot_name: 'event_name'}"

# Publish timed event
ros2 topic pub /log/metadata logging_msgs/msg/LogMetadata \
  "{header: {stamp: {sec: $(date +%s), nanosec: 0}}, \
    timed_event: true, \
    key_name: 'event_name', \
    yaml_metadata: 'key: value'}" --once

# Check disk usage
df -h /data/logs
du -sh /data/logs/*.mcap

# View snapshots
cat /data/logs/snapshots.yaml

# Monitor logs
tail -f /tmp/continuous_logs/*.mcap

# Enable debug logging
ros2 run continuous_logger logger --ros-args --log-level debug

# List all snapshots
ros2 run continuous_logger snapshot_extractor_cli list /data/logs

# Extract a single snapshot
ros2 run continuous_logger snapshot_extractor_cli extract \
  /data/logs event_collision_0 /data/extracted

# Extract all snapshots
ros2 run continuous_logger snapshot_extractor_cli extract-all \
  /data/logs /data/extracted

# Validate log directory
ros2 run continuous_logger snapshot_extractor_cli validate /data/logs

# Play extracted snapshot
ros2 bag play /data/extracted/event_collision_0

# View extracted snapshot info
ros2 bag info /data/extracted/event_collision_0
```

### Parameter Summary

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `log_path` | string | `/tmp/log` | Log directory path |
| `log_prefix` | string | `log_` | Log directory prefix |
| `log_split_time` | int | 30 | Split interval (seconds) |
| `log_split_size` | int | max | Max file size (bytes) |
| `rotation_threshold_percent` | double | 70.0 | Disk usage (%) at which old logs are rotated away |
| `disk_warning_threshold_percent` | double | 70.0 | Warning threshold (%) |
| `disk_check_interval` | double | 60.0 | Check interval (seconds) |
| `topic_keys` | list | `['rosout']` | List of topic identifiers |
| `<key>.name` | string | - | Actual topic name |
| `<key>.throttle` | int | 0 | Message throttle count |
| `event_snapshot_keys` | list | `[]` | List of event snapshot identifiers |
| `<key>.event_name` | string | - | Event name to trigger on |
| `<key>.time_before` | double | 5.0 | Seconds before event |
| `<key>.time_after` | double | 5.0 | Seconds after event |
| `<key>.tags` | list | `[]` | Tags for snapshot |
| `<key>.description` | string | - | Description template |

---

## Implementation Details

### Core Components

#### LogWriter (`log_writer.hpp/cpp`)
- Handles writing MCAP files to disk
- Implements time-based and size-based file splitting
- Writes metadata events to a special `/log/metadata` channel
- Thread-safe write operations
- Automatic topic creation

#### SnapshotWriter (`snapshot_writer.hpp/cpp`)
- Manages snapshot metadata in YAML format
- Stores snapshots in `snapshots.yaml` file in each log directory
- Tracks snapshot state (running vs completed)
- Thread-safe operations
- Supports time overrides for event-triggered snapshots

#### LogManager (`log_manager.hpp/cpp`)
- Monitors log directory for individual MCAP files
- Tracks disk usage and implements rotation policy
- Protects MCAP files overlapping with snapshots from deletion
- Provides disk usage warnings
- Queries MCAP file time ranges to determine snapshot associations

#### LoggerNode (`logger_node.hpp/cpp`)
- Main ROS2 node orchestrating all components
- Generic topic discovery and subscription
- Metadata event handling via `/log/metadata` topic
- Snapshot service at `/log/snapshot`
- Periodic disk usage checking
- Event-triggered snapshot processing
- Configurable parameters for all aspects

#### SnapshotExtractor (`snapshot_extractor.hpp/cpp`)
- CLI tool for extracting snapshots from log files
- Reads MCAP files and snapshots.yaml
- Creates standalone rosbag directories for each snapshot
- Filters messages by timestamp to extract only relevant data
- Preserves snapshot metadata in extracted bags
- Validates log directories before extraction
- Supports batch extraction of all snapshots
- Periodic disk usage checking
- Event-triggered snapshot processing
- Configurable parameters for all aspects

### Event Snapshot Implementation

#### Data Structures

```cpp
struct EventSnapshotConfig {
    std::string event_name;           // Name of the event to trigger on
    double time_before {5.0};          // Seconds before event to include
    double time_after {5.0};           // Seconds after event to include
    std::vector<std::string> tags;    // Tags to apply to the snapshot
    std::string description;          // Description template (can use {event_name})
};

struct PendingEventSnapshot {
    std::string snapshot_id;
    std::string event_name;
    rclcpp::Time trigger_time;
    rclcpp::Time finalize_time;
    std::vector<std::string> tags;
    std::string description;
};
```

#### Unique ID Generation
- Each event type has its own counter (stored in `std::map<std::string, uint64_t>`)
- Counters are initialized from existing snapshots when logger starts
- Parses `snapshots.yaml` and sets counter to highest ID found + 1
- Ensures no duplicate IDs across logger restarts
- IDs format: `event_<event_name>_<counter>`
- Thread-safe access via mutex protection

#### Thread Safety
- Event snapshot list protected by `event_snapshot_mutex_`
- Per-event counters protected by the same mutex
- Snapshot writer operations are internally thread-safe
- Map-based counter ensures unique snapshot IDs per event type

### Performance Considerations

#### Throttling

Use throttling for high-frequency topics to reduce disk usage:
- Camera images: throttle by 10-30
- Laser scans: throttle by 5-10
- Odometry: throttle by 2-5
- Commands: no throttling (throttle: 0)

#### Split Time/Size

Recommended settings:
- Robot operation: 60-300 seconds (1-5 minutes)
- Testing: 30-60 seconds
- Long-term recording: 600-1800 seconds (10-30 minutes)

#### Disk Space

Monitor disk usage regularly:
- Keep rotation_threshold_percent below 85%
- Set warning_threshold_percent 10-15% below max
- Ensure at least 20GB free space for system operations

---

## Troubleshooting

### Logger Not Starting

1. Check that the log directory is writable:
   ```bash
   ls -la /tmp/log
   ```

2. Check for error messages:
   ```bash
   ros2 run continuous_logger logger --ros-args --log-level debug
   ```

### Topics Not Being Logged

1. Check that topics exist:
   ```bash
   ros2 topic list
   ```

2. Enable debug logging to see discovery process:
   ```bash
   ros2 run continuous_logger logger --ros-args --log-level debug
   ```

### Disk Full Issues

1. Check current disk usage:
   ```bash
   df -h /data/logs
   ```

2. Lower the rotation_threshold_percent parameter

3. Check if snapshots are preventing deletion:
   ```bash
   cat /data/logs/snapshots.yaml
   ```

### Snapshot Service Not Responding

1. Check if the service exists:
   ```bash
   ros2 service list | grep snapshot
   ```

2. Test with a ping request:
   ```bash
   ros2 service call /log/snapshot logging_msgs/srv/SnapshotRequest \
     "{request_id: 0, snapshot_name: ''}"
   ```

### Event Not Triggering Snapshot

1. Check event name matches exactly:
   ```bash
   ros2 param get /continuous_logger event_snapshot_keys
   ```

2. Verify timed_event is set to true

3. Check logger output:
   ```bash
   ros2 run continuous_logger logger --ros-args --log-level debug
   ```

### Time Window Issues

- Start time cannot be before log recording began
- Ensure your log split time is longer than your time windows
- Check system clock synchronization

### Duplicate Snapshot IDs

**Symptom:** Logger warns "Failed to start event snapshot"

**Cause:** Snapshot ID already exists

**Solution:** This is expected behavior after restart. The duplicate is rejected, and the original snapshot is preserved. Counters should be initialized properly to avoid this - check that `initializeEventSnapshotCounters()` is being called.

### Snapshot Extraction Fails

**Problem:** `snapshot_extractor_cli` reports errors during extraction

**Common causes:**

1. **Missing snapshots.yaml:**
   ```bash
   ros2 run continuous_logger snapshot_extractor_cli validate /data/logs
   ```

2. **No MCAP files in time range:**
   - Check that MCAP files exist for the snapshot's time range
   - Verify MCAP files haven't been rotated/deleted

3. **Permission issues:**
   ```bash
   # Check read permissions on log directory
   ls -la /data/logs
   
   # Check write permissions on output directory
   ls -la /data/extracted
   ```

4. **Corrupted MCAP files:**
   - Check logs for "Failed to open MCAP file" messages
   - Try extracting individual snapshots to isolate the problem

### Extracted Bag is Empty

**Problem:** Extracted snapshot contains no messages

**Possible causes:**

1. **Time range mismatch:**
   - Verify snapshot times are in epoch seconds (not nanoseconds)
   - Check system clock was synchronized when logs were recorded

2. **MCAP file split boundaries:**
   - Very short snapshots might fall between file splits
   - Consider increasing `log_split_time` parameter

3. **Verify snapshot metadata:**
   ```bash
   cat /data/logs/snapshots.yaml
   ```

---

## Future Enhancements

Based on the design document, future work could include:

1. **Log Reader Component**: 
   - ✅ **Extract snapshots to separate bags** (implemented via `snapshot_extractor_cli`)
   - Convert raw logs to indexed ROS bags
   - Query metadata events
   - Log inspection utility

2. **CLI Tools**:
   - ✅ **Snapshot extraction utility** (implemented)
   - Metadata query tool
   - Advanced log inspection features
   - Snapshot comparison tool

3. **Advanced Features**:
   - Dynamic event configuration (runtime updates)
   - Event filtering (regex or pattern matching)
   - Conditional snapshots (metadata criteria)
   - Snapshot merging (combine overlapping events)
   - Network-based log transfer
   - Compression options
   - Max snapshots limit
   - Event statistics tracking

4. **Testing**:
   - Unit tests for each component
   - Integration tests
   - Performance benchmarks

---

## Dependencies

The package depends on:
- `rclcpp` - ROS2 C++ client library
- `rclcpp_components` - Component architecture
- `logging_msgs` - Custom message definitions
- `rosbag2_cpp` - ROS bag writing
- `rosbag2_storage` - Storage interfaces
- `yaml-cpp` - YAML file handling