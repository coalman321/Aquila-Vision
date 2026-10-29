# Basin

Foxglove Studio panels for the basin runtime, in one extension. Add any of them to a layout from
Studio's **Add panel** menu.

| Panel | What it does |
|-------|--------------|
| [Launch Control](#launch-control) | Monitor and control the processes run by `launch_core` (the [launch TUI](../../basin/launch/tui/)'s job) |
| [Fault Monitor](#fault-monitor) | The live faults reported on `/state/fault_report` (the [fault monitor TUI](../../basin/safety/scripts/fault_monitor_tui.py)'s job) |
| [Continuous Logger](#continuous-logger) | See whether the [continuous logger](../../basin/logging/continuous_logger/) is alive, its disk usage, snapshots and configured events; start and stop snapshots and send it events |

All three read what the `foxglove_bridge` that the robot app launches exposes (`ws://<host>:8765`).
For Studio to describe those messages, the bridge must have `process_msgs`, `status_msgs` and
`logging_msgs` in its runfiles; add them to the app's
`INTERFACE_INDEXES`.

## Launch Control

The same monitoring and control as the launch TUI, in Studio. See the
[launch README](../../basin/launch/README.md) for what the node does with each request.

### What it shows

One card per launch system (the node's hostname, so several machines appear side by side):

- The loaded launch file, how many processes are running, and a **No updates** badge when the
  node has been silent for 3 s.
- A table of each process's ID, name, status, restart count, PID, CPU % and memory.

### What it controls

| Control | Request |
|---------|---------|
| Start / Stop / Restart (per process) | `REQUEST_START` / `REQUEST_STOP` / `REQUEST_RESTART` |
| Launch | `REQUEST_LAUNCH`: start every process |
| Shutdown | `REQUEST_SHUTDOWN`: stop every process (asks to confirm) |
| Hot reload | `REQUEST_HOTLOAD`: stop, re-read the launch file, start again (asks to confirm). The TUI has no binding for this |
| Load config | `REQUEST_LOAD_CONFIG` with a launch file name or absolute path; the node refuses unless the launch is shut down |

Each response's message is logged under the cards, in green for `RESPONSE_OK` and red otherwise.
Controls are disabled while a call to that system is in flight, and everywhere when the data
source can't call services (a recorded file, say): the panel is then read-only.

### Topics and services

| | Name | Type |
|-|------|------|
| Subscribes | `/state/launch/process_list` | `process_msgs/msg/ProcessList` |
| Calls | `/state/launch/<system_name>/request` | `process_msgs/srv/RemoteRequest` |

The service name comes from the `system_name` in each list. The names and enums are in
[src/launch/launchProtocol.ts](src/launch/launchProtocol.ts); keep it in step with
[interfaces/process_msgs](../../interfaces/process_msgs/).

## Fault Monitor

The fault monitor TUI's view, in Studio: a table of every fault that is currently live, one row per
`(system, name)`, sorted by system and then name.

### What it shows

| Column | From `FaultReport` |
|--------|--------------------|
| System, Name | `key.fault_system`, `key.fault_name` |
| Expires in | `expiration_time` minus the current time; yellow under 2 s, so a fault about to lapse stands out |

The source node, the description and the raise time (the start of the fault's current continuous
period) are in the detail pane, not the table.

A fault is **live** until its `expiration_time` passes, and stays live as long as its reporter
keeps sending reports that extend it. A lapsed fault drops out of the list; if it is reported
again it comes back.

### What it does

- **Inspect**: clicking a row selects it and opens a detail pane with the source node, the
  description and the raise and expiry times; Close shuts it. Selection follows the fault, not the
  row position, so it holds when other faults come and go. The pane's status reads **Active** while
  the fault is live and **Inactive** once it has timed out. It stays on its fault when that happens
  (a periodic health check can lapse for a moment between reports) and goes back to Active if a
  new report lands.
- **Dismiss** hides the selected fault, **Dismiss all** every listed one, **Undismiss all** brings
  them back, and **Show dismissed** lists them, greyed and tagged. Dismissal is local to the
  panel: reporters never learn of it and the safety aggregator is unaffected. It holds while the
  fault keeps being re-reported, and is dropped once the fault has lapsed, so a recurrence shows up
  again.

### Time

"Now" is Studio's current time for the data source (`currentTime`), not the browser's wall clock,
so playing back a recording ages faults out as the recording plays, pausing holds them, and seeking
resets the list to what is reported after the seek. On a live connection Studio's clock and the
robot's have to agree for expiry to line up: a Studio clock ahead of the robot's makes faults
lapse early, one behind it keeps them listed too long.

### Topics

| | Name | Type |
|-|------|------|
| Subscribes | `/state/fault_report` | `status_msgs/msg/FaultReport` |

The reports are published transient-local (depth 20 per node) and `foxglove_bridge` adapts its
subscription to the publisher's QoS, so a panel opened after a fault was raised should still get
the last reports. The topic name and message shape are in
[src/faults/faultProtocol.ts](src/faults/faultProtocol.ts); keep it in step with
[interfaces/status_msgs](../../interfaces/status_msgs/).

## Continuous Logger

Watch and drive the [continuous logger](../../basin/logging/continuous_logger/README.md) node
(`continuous_logger`). Everything it shows comes from the status the node publishes about
itself, so a logger that is sending that status is treated as up.

### What it shows

- **Status badge**: **Sending status** while `/log/logger_status` keeps arriving, and **No status**
  once nothing has for 5 s, meaning what follows may be out of date. Before the first status it
  says so instead, or that the topic isn't advertised.
- **Disk**: a bar of the log directory's filesystem, green until the logger's warning threshold,
  yellow until its rotation threshold (`rotation_threshold_percent`, where it starts deleting old logs
  that no snapshot protects) and red beyond, with the free and total space and both thresholds.
- **Snapshots**: what the logger reports: name, status, trigger, start and end time and tags.
  **Running** is a manual snapshot that hasn't been stopped, **Pending** an event snapshot still
  recording its `time_after` window, **Complete** one that is finished. Everything running or
  pending is listed, then the newest completed; a note gives the total when the list is cut short.
  Hovering a name shows its description.
- **Events**: the logger's configured event snapshots (window, tags, and how many snapshots each has
  triggered, counting earlier runs), and the last 30 messages on `/log/metadata`, newest first, from
  any publisher, this panel included.

### What it controls

| Control | Request |
|---------|---------|
| Ping | `REQUEST_NONE`; the logger answers `PONG` |
| Start | `REQUEST_START` with the name, description and tags in the form |
| Stop (on a running snapshot's row) | `REQUEST_STOP`. Pending event snapshots have none: they end themselves |
| Send event | Publishes a `LogMetadata` on `/log/metadata`: the event name (suggesting the configured ones), optional YAML, and a **Timed** box |

A timed event whose name matches a configured event makes the logger start a snapshot around it
(`time_before` / `time_after`); any other event, and every untimed one, is only written into the
log. The panel leaves the message's stamp at zero so the logger stamps it with its own clock: the
snapshot window then lands on the robot's time however far off Studio's clock is. Publishing sends
the message and nothing more; the logger doesn't acknowledge it, so watch the snapshot list (a
pending snapshot appears within a second) to see it took effect.

Starting or stopping a snapshot shows in the list with the next status, within a tick. Each
service response's message is logged under the cards, in green for
`RESPONSE_OK` and red otherwise (an existing name is `RESPONSE_SNAPSHOT_EXISTS`, and names are never
reused, even after the snapshot is stopped). Service controls are disabled while a call is in
flight, and everywhere when the data source can't call services (a recorded file, say); sending
events additionally needs the source to be able to publish. The panel is then read-only.

### Topics and services

| | Name | Type |
|-|------|------|
| Subscribes | `/log/logger_status` | `logging_msgs/msg/LoggerStatus` |
| Subscribes, publishes | `/log/metadata` | `logging_msgs/msg/LogMetadata` |
| Calls | `/log/snapshot` | `logging_msgs/srv/SnapshotRequest` |

The status is published transient-local, so a panel opened between two of them still gets the
latest at once. The names, enums and message shapes are in
[src/logger/loggerProtocol.ts](src/logger/loggerProtocol.ts); keep it in step with
[interfaces/logging_msgs](../../interfaces/logging_msgs/) and
[interfaces/process_msgs](../../interfaces/process_msgs/).

## Try it

```bash
bazel run //<your app>              # launch node + foxglove_bridge
cd foxglove/basin
npm ci && npm run package   # aquila.basin-0.4.0.foxe
```

Install the `.foxe` in Studio (see [foxglove/README.md](../README.md#build-and-install); from a
host shell `npm run local-install` does it in one step), connect to `ws://<host>:8765` and add a
**Launch Control**, **Fault Monitor** or **Continuous Logger** panel.

This extension replaces `aquila.launch-control` (version 0.1.0), which shipped only the launch
panel. Uninstall that one from Studio's Extensions list, or both will offer a Launch Control panel.

## Layout

| File | Role |
|------|------|
| [src/index.ts](src/index.ts) | `activate`: registers the panels |
| [src/common/theme.ts](src/common/theme.ts) | CSS every panel shares: light and dark colors, buttons, badges |
| [src/launch/LaunchControlPanel.tsx](src/launch/LaunchControlPanel.tsx) | Launch Control (React) |
| [src/launch/launchProtocol.ts](src/launch/launchProtocol.ts) | Topic/service names, enums, message parsing; no React |
| [src/launch/launchStyles.ts](src/launch/launchStyles.ts) | Launch Control CSS |
| [src/faults/FaultMonitorPanel.tsx](src/faults/FaultMonitorPanel.tsx) | Fault Monitor (React) |
| [src/faults/faultProtocol.ts](src/faults/faultProtocol.ts) | Topic name, message parsing, expiry and dismissal rules, formatting; no React |
| [src/faults/faultStyles.ts](src/faults/faultStyles.ts) | Fault Monitor CSS |
| [src/logger/ContinuousLoggerPanel.tsx](src/logger/ContinuousLoggerPanel.tsx) | Continuous Logger (React) |
| [src/logger/loggerProtocol.ts](src/logger/loggerProtocol.ts) | Topic/service names, enums, message parsing, formatting; no React |
| [src/logger/loggerStyles.ts](src/logger/loggerStyles.ts) | Continuous Logger CSS |

To add a panel, create `src/<area>/` with the same three pieces, register it in
[src/index.ts](src/index.ts), and add a section above.
