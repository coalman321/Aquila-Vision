# Launch API

A Python API backbone for the launch controller system in ROS2.

## Overview

The `launch_api` package provides a programmatic interface for managing and executing ROS2 launch configurations in production environments. It offers a simplified API for loading, processing, and executing launch descriptions with support for custom process types and configuration staging.

## Features

- **Launch File Loading**: Load and parse ROS2 launch files programmatically
- **Process Management**: Abstract process definitions with support for different process types
- **Configuration Staging**: Utilities for managing configuration files in production environments
- **Flexible Execution**: Execute launch descriptions both within ROS2 and as standalone Python processes

## Package Structure

```
launch_api/
├── actions/                    # Process action definitions
│   ├── node.py                # ROS2 node process type
│   └── process.py             # Abstract process base class
├── descriptions/              # Launch description handling
│   ├── description.py         # Base description class
│   └── load_description.py    # Launch file loader
├── utils/                     # Utility modules
│   └── config_stager.py       # Configuration staging utilities
└── loader.py                  # Main production loader class
```

## Installation

This package is built with Bazel as part of the workspace (it has no
`package.xml`/`setup.py`). Depend on it from another target as
`//basin/launch/api:launch_api`, or build it directly:

```bash
bazel build //basin/launch/api:launch_api
```

Its `launch/` directory is published under the `launch_api`
ament package (`:ament_index`), which is how `(package, launch_file)` pairs
resolve. It is normally imported by the embedded interpreter in
[launch_core](../core/), not run on its own.

## Usage

### Basic Usage

```python
from launch_api.loader import ProductionLoader

# Initialize the loader
loader = ProductionLoader()

# Load a launch file
loader.load_file("/path/to/your/launch_file.py")

# Print the loaded description
loader.print_desc()

# Get the flat description list
processes = loader.get_flat_desc()

# Execute processes in Python (for testing)
loader.execute_in_python()
```

### Loading Launch Files

The API supports loading launch files in two ways:

1. **Direct file path**:
```python
from launch_api.descriptions.load_description import LoadDescription

# Load by absolute file path
desc = LoadDescription("/absolute/path/to/launch_file.py")
processes = desc.load_file()
```

2. **Package-relative path**:
```python
# Load from package share directory
desc = LoadDescription("package_name", "launch_file.py")
processes = desc.load_file()
```

### Custom Process Types

Create custom process types by extending the `Process` abstract base class:

```python
from launch_api.actions.process import Process
from typing import List

class MyCustomProcess(Process):
    def get_executable(self) -> str:
        return "/path/to/executable"
    
    def get_args(self) -> List[str]:
        return ["--arg1", "value1"]
    
    def get_type(self) -> str:
        return "custom"
    
    def get_name(self) -> str:
        return "my_process"
    
    def get_sanitized_name(self) -> str:
        return "my_process_sanitized"
```

### Scheduler Settings

A `Node` takes an optional `scheduling` argument that sets its Linux scheduler policy, priority and nice value. The launch node applies these to the child process before it executes, so every thread the node creates inherits them. Without the argument, the process inherits the launch node's own settings.

```python
from launch_api.actions.node import Node
from launch_api.actions.scheduling import Scheduling

Node("controller", "my_package", scheduling=Scheduling("fifo", priority=80))
Node("logger", "my_package", scheduling=Scheduling("batch", nice=10))
```

| Policy | `priority` | `nice` |
|---|---|---|
| `other` (default), `batch` | 0 | -20 to 19, or `None` to inherit |
| `idle` | 0 | not allowed |
| `fifo`, `rr` | 1 to 99 | not allowed |

`Scheduling` checks these values when the launch file loads. Real-time policies and negative nice values need `CAP_SYS_NICE` or a sufficient `RLIMIT_RTPRIO`/`RLIMIT_NICE`. If the kernel refuses a setting, the process does not start. The launch node logs the reason and handles the failure like a crash, retrying up to `num_restarts` times. Child processes forked by the node fall back to the default policy (`SCHED_RESET_ON_FORK`).

A custom `Process` subclass can override `get_scheduling()` to return a `Scheduling`.

## Launch File Format

Launch files should follow the production launch format and include a `generate_launch_description()` function:

```python
from launch_api.actions.node import Node
from launch_api.descriptions.load_description import LoadDescription

def generate_launch_description() -> list:
    return [
        Node(
            "node_name",
            "package_name",
            namespace="/my_namespace",
            params=[],
        ),
        LoadDescription(
            "another_package",
            "another_launch_file.py"
        ),
    ]
```

## API Reference

### ProductionLoader

Main class for loading and managing launch descriptions.

- `load_file(file: os.PathLike)`: Load a launch file
- `print_desc()`: Print loaded process descriptions
- `get_flat_desc() -> list`: Get list of loaded processes
- `execute_in_python()`: Execute processes as subprocesses (testing only)

### Process (Abstract Base Class)

Base class for all process types.

**Abstract Methods:**
- `get_executable() -> str`: Get executable path
- `get_args() -> List[str]`: Get process arguments
- `get_type() -> str`: Get process type identifier
- `get_name() -> str`: Get ROS process name
- `get_sanitized_name() -> str`: Get sanitized process name

### LoadDescription

Class for loading launch descriptions from files.

- `__init__(package: os.PathLike, file: str = None)`: Initialize with package/file
- `load_file() -> list`: Load and return process list
- `get_resolved_path() -> os.PathLike`: Get resolved file path

## Configuration

The package supports configuration staging through the `config_stager` utility, allowing for flexible configuration management in production environments.

## Development

### Testing

Run tests using pytest:

```bash
python -m pytest test/
```

### Code Style

This package follows PEP 257 for docstrings and uses flake8 for linting.

## License

Proprietary

## Maintainer

**Cole Tucker**  
Email: cjtucker321@gmail.com

## Version

1.0.0

---

For more information about the launch system, see the related packages:
- `launch_core`: Main launch controller
- `launch_tui`: Terminal user interface