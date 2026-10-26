# Launch

A ROS2 package that provides remote launch control system via a ROS interface. This package enables centralized management and monitoring of production ROS2 applications through a dedicated launch system that supports both regular and daemon processes.

## Overview

The launch system consists of several key components:

- **LaunchNode**: Core ROS2 node that manages process lifecycle
- **LaunchInterpreter**: Python-based launch file interpreter using pybind11
- **ControlledProcess**: Process management and monitoring wrapper
- **Remote API**: Service-based remote control interface

## Features

- **Process Management**: Start, stop, and monitor ROS2 nodes and arbitrary processes
- **Daemon Support**: Run background processes alongside main applications
- **Python Launch Files**: Use Python-based `.prod.py` launch files for flexible configuration
- **Process Monitoring**: Built-in watchdog functionality with configurable timeouts
- **Remote Control**: Service-based interface for remote launch control
- **Heartbeat Monitoring**: Process health monitoring with automatic restart capabilities
- **Resource Monitoring**: CPU and memory usage tracking for managed processes

## Dependencies

- **ROS2 Packages**:
  - `rclcpp` - ROS2 C++ client library
  - [`process_msgs`](../../interfaces/process_msgs/) - Custom process monitoring messages
  - [`process_lib`](../core/process_lib/) - Process monitoring utilities
  - `ament_index_cpp` - Package resource indexing

- **Bazel Dependencies** (declared in the workspace `MODULE.bazel`; nothing to install):
  - `pybind11_bazel` - Provides `pybind11` for embedding the Python interpreter
  - The workspace's hermetic Python 3.12 (linked in by the node, and run by the launcher)

## Installation

This package is built with Bazel as part of the workspace (see the
[root README](../../README.md)); it has no `package.xml`/`CMakeLists.txt`:

```bash
bazel build //basin/launch/...
```

## Usage

### Basic Usage

Run the launch node with the default configuration:

```bash
bazel run //basin/launch/core:launch_node_exe
```

`:launch_node_exe` is a small Python launcher that first gives the node's
embedded Python interpreter the standard library and `launch_api` search paths
it needs (what sourcing an ament/colcon install did before), then execs the
node (`:launch_node_bin`). Anything after `--` is passed through to the node.

### Configuration Parameters

| Parameter | Type | Default | Description |
|-----------|------|---------|-------------|
| `launch_file` | string | `"test.prod.py"` | Name of the launch file |
| `launch_package` | string | `"launch_api"` | Package containing the launch file |
| `daemon_file` | string | `""` | Optional daemon launch file name |
| `daemon_package` | string | `""` | Package containing the daemon launch file |
| `num_restarts` | long | `5` | Maximum number of automatic restarts per process |
| `watchdog_delay_s` | double | `6.0` | Watchdog timeout in seconds |

### Example Launch with Parameters

```bash
bazel run //basin/launch/core:launch_node_exe -- --ros-args \
    -p launch_file:=my_app.prod.py -p launch_package:=my_package \
    -p daemon_file:=background_services.prod.py -p daemon_package:=my_package \
    -p num_restarts:=10 -p watchdog_delay_s:=8.0
```

The same parameters can go in a params file (`--ros-args --params-file my_config.yaml`).
To bake your own params file and launch file's runfiles into an application binary,
wrap `:launch_node_bin` in a `python_env_binary` the way other apps do.

An absolute path can be given as `launch_file` with `launch_package:=''`.

### Production Launch Files

Production launch files use a Python-based format with the `.prod.py` extension. These files should contain a `generate_launch_description()` function that returns a list of processes to launch.

Example `my_app.prod.py`:

```python
from launch_api.actions.node import Node

def generate_launch_description() -> list:
    return [
        Node(
            executable="talker",
            package_name="demo_nodes_cpp",
            namespace="/my_app",
            params=[],
        ),
        Node(
            executable="listener", 
            package_name="demo_nodes_cpp",
            namespace="/my_app",
            params=[],
        ),
    ]
```

### Remote Control Interface

The node provides a service interface for remote control:

```bash
# Start the launched processes
ros2 service call /production_launch/remote_request process_msgs/srv/RemoteRequest "{command: 'start'}"

# Stop the launched processes  
ros2 service call /production_launch/remote_request process_msgs/srv/RemoteRequest "{command: 'stop'}"

# Get status information
ros2 service call /production_launch/remote_request process_msgs/srv/RemoteRequest "{command: 'status'}"
```

### Process Monitoring

The system publishes process status information and heartbeats:

```bash
# Monitor process list
ros2 topic echo /production_launch/process_list

# Monitor process heartbeats
ros2 topic echo /production_launch/process_heartbeat
```

## Architecture

### Node Structure

```
LaunchNode
├── LaunchInterpreter (Python integration)
├── ControlledProcess[] (Process management)
├── Service Server (Remote control)
├── Publishers (Status/Heartbeat)
└── Timers (Watchdog/Monitoring)
```

### Process Lifecycle

1. **Load**: Parse production launch file using Python interpreter
2. **Start**: Spawn controlled processes with monitoring
3. **Monitor**: Continuous health checking and resource monitoring
4. **Restart**: Automatic restart on failure (up to configured limit)
5. **Stop**: Graceful shutdown of all managed processes

## Testing

The package includes comprehensive unit tests:

```bash
bazel test //basin/launch/...
```

Two tests cover different things:

- `:launch_test` is a `py_test` that runs a gtest binary
  (`:launch_test_bin`, tagged `manual`) linking the node library directly,
  under the same launcher as above. It covers the interpreter and process
  control classes.
- `:launch_node_test` runs `:launch_node_exe` itself against a stub launch file
  ([test/sleeper.prod.py](./test/sleeper.prod.py)): it checks the node starts
  the child the launch file describes, and stopping the node (SIGINT) stops the
  child too. It covers what the gtest can't: the deployed runtime environment.
 The tests that load `test.prod.py`/`test2.prod.py`
only check that `demo_nodes_cpp`'s `talker`/`listener` resolve to files, so
they use stub executables rather than the real package.

Test coverage includes:
- Launch interpreter functionality
- Controlled process management
- Service interface validation
- Process lifecycle testing

## Integration

This package works in conjunction with:

- **launch_api**: Python API for creating launch files
- **launch_tui**: Terminal UI for interactive control
- **process_msgs**: Message definitions for process communication
- **process_lib**: Low-level process monitoring utilities

## Troubleshooting

### Common Issues

1. **Python Library Not Found**: Ensure Python development libraries are installed
2. **Launch File Not Found**: Verify package and file names are correct
3. **Process Start Failures**: Check executable paths and permissions
4. **Watchdog Timeouts**: Adjust `watchdog_delay_s` for slower starting processes

### Debug Mode

Enable debug logging:

```bash
bazel run //basin/launch/core:launch_node_exe -- --ros-args --log-level debug
```

## License

Proprietary - See package.xml for details.

## Maintainer

Cole Tucker (cjtucker321@gmail.com)

## Version

1.0.0