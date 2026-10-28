# Basin Interfaces

ROS 2 message and service definitions shared across the basin robot stack.

## Packages

| Package | Purpose |
| --- | --- |
| `calibration_msgs` | Camera calibration session status/requests |
| `driver_msgs` | Hardware driver configuration/commanding services |
| `generic_msgs` | Generic stamped primitive wrappers |
| `localization_msgs` | GNSS/graph-SLAM/terrain localization messages |
| `logging_msgs` | Log snapshot control, fault-triggered pcap captures |
| `process_msgs` | Remote process monitoring/control, execution determinism records |
| `status_msgs` | Fault reporting and safety/killswitch status |
| `timesync_msgs` | PTP time-sync status |
| `basler_driver_msgs` | Basler camera driver status (//drivers/basler_driver) |

These are Bazel-only: each package is just its `.msg`/`.srv` files plus a
`BUILD.bazel` under each `msg/`/`srv/` directory declaring
`ros_message`/`ros_service` targets — no `package.xml`/`CMakeLists.txt`, so
they're not colcon-buildable. See the root
[README.md](../README.md#basin-interfaces) for how to reference a target.
