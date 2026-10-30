# log_util_py

> **Bazel:** this package is built with Bazel in this monorepo. The `colcon`/`ros2 run`/`ros2 launch` commands and `package.xml` snippets below are from the original colcon build; see the [Quick Start](../README.md#quick-start) for the Bazel equivalents.

Python bindings for `log_util` - Query and extract snapshots from ROS2 bag logs.

## Table of Contents

- [Overview](#overview)
- [Installation](#installation)
- [Quick Start](#quick-start)
- [API Reference](#api-reference)
- [Utility Functions](#utility-functions)
- [Command Line Tool](#command-line-tool)
- [Common Use Cases](#common-use-cases)
- [Examples](#examples)
- [Troubleshooting](#troubleshooting)
- [Technical Details](#technical-details)

## Overview

This package provides Python bindings to the C++ continuous logging library, allowing you to:
- ✓ List and query snapshots from log directories
- ✓ Extract snapshots to separate rosbag files
- ✓ Filter snapshots by tags, time ranges, and triggers
- ✓ Manage log directories and monitor disk usage
- ✓ Find which MCAP files contain specific time ranges
- ✓ Validate log directory structure

### What is a Snapshot?

A snapshot is a marked time range in continuous ROS2 bag recordings that captures important events or data. Each snapshot contains:
- **ID**: Unique identifier
- **Time Range**: Start and end times (epoch seconds)
- **Trigger**: What caused the snapshot (manual, event-based, etc.)
- **Description**: Human-readable description
- **Tags**: Categorization labels

## Installation

### Prerequisites

- Python 3.8+
- ROS2 Humble or newer
- pybind11 (install with `sudo apt-get install python3-pybind11`)
- log_util package

### Build the Package

```bash
cd /home/coalman321/frc-2026
colcon build --packages-select log_util_py
source install/setup.bash
```

### Verify Installation

```bash
python3 -c "from log_util_py import SnapshotExtractor; print('✓ Installation successful')"
```

## Quick Start

### Example 1: List All Snapshots

```python
from log_util_py.utils import list_snapshots

snapshots = list_snapshots('/path/to/logs')

print(f"Found {len(snapshots)} snapshots:\n")
for snap in snapshots:
    duration = snap.end_time - snap.start_time
    print(f"{snap.id}: {snap.description} ({duration}s)")
```

### Example 2: Find Critical Snapshots

```python
from log_util_py.utils import find_snapshots_by_tag

critical = find_snapshots_by_tag('/path/to/logs', 'critical')
for snap in critical:
    print(f"Critical: {snap.id} - {snap.description}")
```

### Example 3: Extract a Snapshot

```python
from log_util_py.utils import extract_snapshot

success = extract_snapshot(
    log_directory='/path/to/logs',
    snapshot_id='snapshot_001',
    output_directory='/path/to/output'
)

if success:
    print("Snapshot extracted successfully!")
```

### Example 4: Find Recent Snapshots

```python
from log_util_py.utils import find_snapshots_by_time_range
import time

now = int(time.time())
hour_ago = now - 3600

recent = find_snapshots_by_time_range('/path/to/logs', hour_ago, now)
print(f"Found {len(recent)} snapshots in the last hour")
```

## API Reference

### Core Classes

#### SnapshotInfo

Data class containing snapshot metadata.

**Attributes:**
- `id` (str): Unique identifier
- `start_time` (int): Start time in epoch seconds
- `end_time` (int): End time in epoch seconds
- `trigger` (str): Trigger type ('manual' or 'event-<trigger_id>')
- `description` (str): User-defined description
- `tags` (List[str]): List of tags for categorization

**Example:**
```python
from log_util_py import SnapshotInfo

info = SnapshotInfo()
info.id = "my_snapshot"
info.start_time = 1000
info.end_time = 2000
info.trigger = "manual"
info.description = "Important event"
info.tags = ["critical", "analysis-required"]
```

#### SnapshotExtractor

Main class for querying and extracting snapshots.

**Methods:**

##### `list_snapshots(log_directory: str) -> List[SnapshotInfo]`
Lists all snapshots in a log directory.

```python
extractor = SnapshotExtractor()
snapshots = extractor.list_snapshots('/path/to/logs')
```

##### `extract_snapshot(log_directory: str, snapshot_id: str, output_directory: str) -> bool`
Extracts a single snapshot to a new rosbag directory.

```python
success = extractor.extract_snapshot('/path/to/logs', 'snapshot_001', '/output')
```

##### `extract_all_snapshots(log_directory: str, output_base_directory: str) -> int`
Extracts all snapshots from a log directory. Returns number of snapshots extracted.

```python
count = extractor.extract_all_snapshots('/path/to/logs', '/output')
```

##### `validate_log_directory(log_directory: str) -> Tuple[bool, str]`
Validates that a log directory has the required files. Returns (is_valid, error_message).

```python
valid, error = extractor.validate_log_directory('/path/to/logs')
if not valid:
    print(f"Invalid: {error}")
```

##### `find_relevant_mcap_files(log_directory: str, start_time: int, end_time: int) -> List[str]`
Finds all MCAP files that overlap with a time range.

```python
mcap_files = extractor.find_relevant_mcap_files('/path/to/logs', 1000, 2000)
```

#### SnapshotWriter

Manages snapshot metadata files.

**Methods:**

##### `set_recording_directory(directory: str)`
Sets the recording directory where snapshots are stored.

##### `load_from_file() -> bool`
Loads existing snapshots from the YAML file.

##### `get_snapshot(snapshot_id: str) -> SnapshotInfo`
Gets information about a specific snapshot.

##### `get_all_snapshots() -> List[SnapshotInfo]`
Gets all snapshots.

##### `has_snapshot(snapshot_id: str) -> bool`
Checks if a snapshot exists.

##### `is_snapshot_running(snapshot_id: str) -> bool`
Checks if a snapshot is currently running (not yet stopped).

**Example:**
```python
writer = SnapshotWriter()
writer.set_recording_directory('/path/to/logs')
writer.load_from_file()

all_snapshots = writer.get_all_snapshots()
if writer.has_snapshot('snapshot_001'):
    info = writer.get_snapshot('snapshot_001')
```

#### LogManager

Manages log directories and disk usage.

**Methods:**

##### `set_log_directory(directory: str)`
Sets the log directory to manage.

##### `set_disk_limits(rotation_threshold_percent: float, warning_threshold_percent: float)`
Sets disk usage limits (both values should be 0-100).

##### `scan_log_directory() -> int`
Scans the log directory for MCAP files and snapshots. Returns number of MCAP files found.

##### `get_log_files() -> List[str]`
Gets the list of all MCAP file paths.

##### `get_snapshot_log_files(snapshot_id: str) -> List[str]`
Gets MCAP files associated with a specific snapshot.

##### `get_disk_usage_percent() -> float`
Gets the current disk usage percentage (0-100).

##### `check_disk(active_log_file: str = "") -> DiskCheckResult`
Rescans the directory and deletes the oldest logs no snapshot protects until disk usage is under the rotation
threshold, never `active_log_file`. The result has `rotated_files`, `disk_usage_percent`, `rotation_blocked`,
`protected_bytes`, `snapshot_usage_percent` and `snapshot_usage_high`.

##### `get_last_disk_check() -> DiskCheckResult`
The result of the most recent `check_disk`.

##### `prepare_log_directory()` / `next_log_name(prefix: str) -> str`
Create the log directory, and name the next run's log one past the highest run number on disk.

**Example:**
```python
manager = LogManager()
manager.set_log_directory('/path/to/logs')
manager.set_disk_limits(rotation_threshold_percent=80, warning_threshold_percent=70)

num_logs = manager.scan_log_directory()
usage = manager.get_disk_usage_percent()
print(f"Found {num_logs} MCAP files, disk usage: {usage:.1f}%")
```

## Utility Functions

The `log_util_py.utils` module provides convenient wrapper functions for common operations.

### `list_snapshots(log_directory: str) -> List[SnapshotInfo]`
List all snapshots in a log directory.

```python
from log_util_py.utils import list_snapshots
snapshots = list_snapshots('/path/to/logs')
```

### `find_snapshots_by_tag(log_directory: str, tag: str) -> List[SnapshotInfo]`
Find all snapshots that have a specific tag.

```python
from log_util_py.utils import find_snapshots_by_tag
critical = find_snapshots_by_tag('/path/to/logs', 'critical')
```

### `find_snapshots_by_time_range(log_directory: str, start_time: int, end_time: int) -> List[SnapshotInfo]`
Find all snapshots that overlap with a given time range.

```python
from log_util_py.utils import find_snapshots_by_time_range
import time

now = int(time.time())
hour_ago = now - 3600
recent = find_snapshots_by_time_range('/path/to/logs', hour_ago, now)
```

### `find_snapshots_by_trigger(log_directory: str, trigger: str) -> List[SnapshotInfo]`
Find all snapshots triggered by a specific trigger.

```python
from log_util_py.utils import find_snapshots_by_trigger
manual = find_snapshots_by_trigger('/path/to/logs', 'manual')
event_based = find_snapshots_by_trigger('/path/to/logs', 'event-collision')
```

### `extract_snapshot(log_directory: str, snapshot_id: str, output_directory: str) -> bool`
Extract a single snapshot to a rosbag.

```python
from log_util_py.utils import extract_snapshot
success = extract_snapshot('/path/to/logs', 'snapshot_001', '/output')
```

### `extract_all_snapshots(log_directory: str, output_base_directory: str) -> int`
Extract all snapshots from a log directory. Returns number extracted.

```python
from log_util_py.utils import extract_all_snapshots
count = extract_all_snapshots('/path/to/logs', '/output')
```

### `validate_log_directory(log_directory: str) -> Tuple[bool, str]`
Validate that a log directory has the required files.

```python
from log_util_py.utils import validate_log_directory
valid, error = validate_log_directory('/path/to/logs')
```

### `get_snapshot_info(log_directory: str, snapshot_id: str) -> Optional[SnapshotInfo]`
Get information about a specific snapshot.

```python
from log_util_py.utils import get_snapshot_info
info = get_snapshot_info('/path/to/logs', 'snapshot_001')
if info:
    print(f"Duration: {info.end_time - info.start_time} seconds")
```

## Command Line Tool

The package includes a command-line tool for common operations. Located at `examples/query_snapshots.py`.

### Usage

```bash
python3 examples/query_snapshots.py <command> [arguments]
```

### Commands

#### `list`
List all snapshots with details.

```bash
python3 examples/query_snapshots.py list /path/to/logs
```

#### `filter-tag`
Filter snapshots by tag.

```bash
python3 examples/query_snapshots.py filter-tag /path/to/logs critical
```

#### `filter-trigger`
Filter snapshots by trigger type.

```bash
python3 examples/query_snapshots.py filter-trigger /path/to/logs manual
```

#### `filter-time`
Filter snapshots by time range.

```bash
python3 examples/query_snapshots.py filter-time /path/to/logs 1696531200 1696534800
```

#### `extract`
Extract a specific snapshot.

```bash
python3 examples/query_snapshots.py extract /path/to/logs snapshot_001 /output
```

#### `extract-all`
Extract all snapshots.

```bash
python3 examples/query_snapshots.py extract-all /path/to/logs /output
```

#### `info`
Show log directory information and statistics.

```bash
python3 examples/query_snapshots.py info /path/to/logs
```

## Common Use Cases

### Use Case 1: Export Snapshots to CSV

```python
import csv
from log_util_py.utils import list_snapshots

snapshots = list_snapshots('/path/to/logs')

with open('snapshots.csv', 'w', newline='') as f:
    writer = csv.writer(f)
    writer.writerow(['ID', 'Start Time', 'End Time', 'Duration', 'Trigger', 'Description', 'Tags'])
    
    for snap in snapshots:
        duration = snap.end_time - snap.start_time
        writer.writerow([
            snap.id,
            snap.start_time,
            snap.end_time,
            duration,
            snap.trigger,
            snap.description,
            ';'.join(snap.tags)
        ])

print(f"Exported {len(snapshots)} snapshots to snapshots.csv")
```

### Use Case 2: Batch Extract Tagged Snapshots

```python
from log_util_py.utils import find_snapshots_by_tag, extract_snapshot

# Find all snapshots with 'analysis-required' tag
snapshots = find_snapshots_by_tag('/path/to/logs', 'analysis-required')

# Extract each one
for snap in snapshots:
    print(f"Extracting {snap.id}...")
    output_dir = f'/path/to/output/{snap.id}'
    success = extract_snapshot('/path/to/logs', snap.id, output_dir)
    
    if success:
        print(f"  ✓ Extracted to {output_dir}")
    else:
        print(f"  ✗ Failed to extract {snap.id}")
```

### Use Case 3: Monitor Disk Usage

```python
from log_util_py import LogManager

manager = LogManager()
manager.set_log_directory('/path/to/logs')
manager.set_disk_limits(rotation_threshold_percent=80, snapshot_usage_threshold_percent=50)

disk = manager.check_disk()
print(f"Disk usage: {disk.disk_usage_percent:.1f}%")
if disk.rotated_files:
    print(f"✓ Rotated {disk.rotated_files} old logs")
if disk.snapshot_usage_high:
    print(f"⚠ Snapshots fill {disk.snapshot_usage_percent:.1f}% of the disk!")
```

### Use Case 4: Find and Analyze Long Duration Snapshots

```python
from log_util_py.utils import list_snapshots

snapshots = list_snapshots('/path/to/logs')

# Find snapshots longer than 5 minutes
long_snapshots = [s for s in snapshots if (s.end_time - s.start_time) > 300]

print(f"Found {len(long_snapshots)} snapshots longer than 5 minutes:")
for snap in sorted(long_snapshots, key=lambda s: s.end_time - s.start_time, reverse=True):
    duration = snap.end_time - snap.start_time
    print(f"  {snap.id}: {duration}s - {snap.description}")
```

### Use Case 5: Query Snapshot Coverage

```python
from log_util_py import SnapshotExtractor

extractor = SnapshotExtractor()
snapshots = extractor.list_snapshots('/path/to/logs')

print("Snapshot Coverage Report:\n")
for snap in snapshots:
    # Find MCAP files that contain this snapshot
    mcap_files = extractor.find_relevant_mcap_files(
        '/path/to/logs',
        snap.start_time,
        snap.end_time
    )
    
    duration = snap.end_time - snap.start_time
    print(f"{snap.id}:")
    print(f"  Duration: {duration}s")
    print(f"  MCAP files: {len(mcap_files)}")
    print(f"  Trigger: {snap.trigger}")
    print()
```

## Examples

The package includes several example scripts in the `examples/` directory:

### 1. simple_query.py
Basic example showing how to list snapshots.

```bash
# Edit the LOG_DIR variable in the script, then run:
python3 examples/simple_query.py
```

### 2. query_snapshots.py
Full-featured command-line tool with multiple commands. See [Command Line Tool](#command-line-tool) section.

### 3. comprehensive_demo.py
Demonstrates all features of the package.

```bash
python3 examples/comprehensive_demo.py /path/to/logs
```

This demo shows:
1. Directory validation
2. Listing snapshots
3. Filtering by tag, trigger, and time
4. Using LogManager
5. Using SnapshotExtractor
6. Using SnapshotWriter
7. Getting specific snapshot info

## Troubleshooting

### ImportError: No module named 'log_util_py'

**Problem:** Python cannot find the package.

**Solution:** Make sure you've sourced the workspace:
```bash
source /home/coalman321/frc-2026/install/setup.bash
```

### Build Fails: pybind11 not found

**Problem:** pybind11 development headers are not installed.

**Solution:** Install pybind11:
```bash
sudo apt-get install python3-pybind11
```

Or via pip:
```bash
pip3 install pybind11
```

### Symbol Not Found Errors

**Problem:** Shared library symbols cannot be resolved.

**Solution:** Rebuild both packages:
```bash
colcon build --packages-select log_util log_util_py
source install/setup.bash
```

### No Snapshots Found

**Problem:** `list_snapshots()` returns an empty list.

**Solution:** Make sure your log directory contains:
- A `snapshots.yaml` file
- One or more `.mcap` files

Validate the directory:
```python
from log_util_py.utils import validate_log_directory
valid, error = validate_log_directory('/path/to/logs')
if not valid:
    print(f"Invalid directory: {error}")
```

### Extraction Fails

**Problem:** `extract_snapshot()` returns False.

**Possible causes:**
1. Snapshot ID doesn't exist
2. Output directory is not writable
3. Required MCAP files are missing or corrupted

**Solution:** Check the snapshot exists and output directory is writable:
```python
from log_util_py.utils import get_snapshot_info, validate_log_directory
import os

# Verify snapshot exists
info = get_snapshot_info('/path/to/logs', 'snapshot_001')
if not info:
    print("Snapshot doesn't exist!")

# Verify log directory is valid
valid, error = validate_log_directory('/path/to/logs')
if not valid:
    print(f"Invalid log directory: {error}")

# Verify output directory is writable
output_dir = '/path/to/output'
os.makedirs(output_dir, exist_ok=True)
if not os.access(output_dir, os.W_OK):
    print("Output directory is not writable!")
```

## Technical Details

### Architecture

The package uses **pybind11** to create Python bindings for the C++ `log_util` library. The architecture consists of:

1. **C++ Bindings Layer** (`src/bindings.cpp`): Maps C++ classes to Python
2. **Python Package** (`log_util_py/`): Python module structure
3. **Utility Layer** (`log_util_py/utils.py`): Pythonic convenience functions
4. **Example Scripts** (`examples/`): Demonstration code

### Build System

- **CMake**: Builds the C++ pybind11 extension module
- **ament_cmake_python**: Integrates with ROS2 build system
- **setuptools**: Manages Python package installation

### Dependencies

**Build Dependencies:**
- ament_cmake
- ament_cmake_python
- log_util
- rclcpp
- logging_msgs
- rosbag2_cpp
- rosbag2_storage
- yaml-cpp
- Python 3 Development headers
- pybind11

**Runtime Dependencies:**
- Python 3.8+
- log_util (shared library)
- ROS2 runtime libraries

### Type Conversions

pybind11 automatically handles type conversions between C++ and Python:

| C++ Type | Python Type |
|----------|-------------|
| `std::string` | `str` |
| `int64_t` | `int` |
| `bool` | `bool` |
| `std::vector<std::string>` | `List[str]` |
| `std::vector<SnapshotInfo>` | `List[SnapshotInfo]` |
| `std::filesystem::path` | `str` |

### Memory Management

pybind11 handles memory management automatically:
- C++ objects owned by Python have their lifetime managed by Python's garbage collector
- References to C++ objects are properly tracked
- No manual memory management required in Python code

### Thread Safety

The underlying C++ library uses mutexes for thread safety. The Python bindings preserve this behavior, making it safe to use from multiple Python threads (subject to Python's GIL).

### Performance

The bindings add minimal overhead:
- Function call overhead: < 1 microsecond
- Type conversion: Negligible for basic types
- Large data transfers (lists): Linear with size, but still efficient
- File I/O operations: Dominated by disk I/O, not binding overhead

For best performance when processing many snapshots, use batch operations where available (e.g., `extract_all_snapshots()` instead of looping with `extract_snapshot()`).

## Package Structure

```
log_util_py/
├── CMakeLists.txt              # Build configuration
├── package.xml                 # ROS2 package manifest
├── setup.py                    # Python package setup
├── README.md                   # This file
├── resource/
│   └── log_util_py    # Ament resource marker
├── log_util_py/       # Python package
│   ├── __init__.py            # Package exports
│   └── utils.py               # Utility functions
├── src/
│   └── bindings.cpp           # pybind11 C++ bindings
├── examples/
│   ├── simple_query.py        # Basic example
│   ├── query_snapshots.py     # Full CLI tool
│   └── comprehensive_demo.py  # Complete demo
└── test/
    └── test_bindings.py       # Unit tests
```

## Contributing

When adding new functionality:

1. **C++ Bindings**: Add to `src/bindings.cpp` using pybind11 syntax
2. **Utility Functions**: Add convenience wrappers to `log_util_py/utils.py`
3. **Examples**: Add example usage to `examples/`
4. **Tests**: Add tests to `test/test_bindings.py`
5. **Documentation**: Update this README

## Quick Reference

### Import
```python
from log_util_py import SnapshotExtractor, SnapshotWriter, LogManager
from log_util_py.utils import list_snapshots, find_snapshots_by_tag
```

### Common Operations
```python
# List all snapshots
snapshots = list_snapshots('/path/to/logs')

# Filter by tag
critical = find_snapshots_by_tag('/path/to/logs', 'critical')

# Extract snapshot
success = extract_snapshot('/path/to/logs', 'snapshot_001', '/output')

# Validate directory
valid, error = validate_log_directory('/path/to/logs')
```

### CLI Tool
```bash
# List snapshots
python3 examples/query_snapshots.py list /path/to/logs

# Filter by tag
python3 examples/query_snapshots.py filter-tag /path/to/logs critical

# Extract
python3 examples/query_snapshots.py extract /path/to/logs snapshot_001 /output
```

## License

MIT - see the repository root `LICENSE`.

## Maintainer

Cole Tucker (cjtucker321@gmail.com)

## Version

0.0.0

---

**Happy logging! 🚀**
