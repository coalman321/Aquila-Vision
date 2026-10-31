# Changelog

## 0.5.0

- TagSolution converter: `localization_msgs/msg/TagSolution` converts to `foxglove.PoseInFrame`,
  so the 3D panel draws the multitag solution in the `field` frame. Solutions with no detected tags
  are skipped.

## 0.4.1

- Continuous Logger: shows how much of the disk snapshot-protected logs fill against the logger's
  snapshot usage threshold, and colours the disk bar by it. Reads the `LoggerStatus` fields
  `snapshot_usage_percent`, `snapshot_usage_threshold_percent` and `snapshot_protected_bytes`,
  which replace `disk_warning_threshold_percent`; with an older logger snapshot usage reads as unset.
- Continuous Logger: reads the rotation threshold from the renamed `LoggerStatus` field
  `disk_rotation_threshold_percent` (was `disk_max_usage_percent`). Needs a logger built after the
  rename; with an older one the threshold reads as unset.

## 0.4.0

- Continuous Logger: reads the logger's own status (`/log/logger_status`) and shows the disk
  usage against its warning and rotation thresholds, every snapshot it holds (running, pending and
  the newest completed) with a Stop on the running ones, and the event snapshots it is configured
  for with how often each has fired. It no longer tracks only the snapshots it started itself, and
  the stop-by-name field is gone: every running snapshot has its own Stop. Needs a logger that
  publishes the status. Whether the logger is up is now just whether that status is arriving
  (**Sending status** / **No status**); the panel no longer reads the process heartbeat.

## 0.3.0

- Continuous Logger panel: the logger's liveness (from its process heartbeat), start/stop of manual
  snapshots through `/log/snapshot`, and the events on `/log/metadata` with a form to send timed
  events that trigger the logger's event snapshots.

## 0.2.1

- Fault Monitor: the table shows only system, name and time to expiry; the source node,
  description and raise time are in the detail pane, which now reads Active or Inactive for status.
- Fault Monitor: no keyboard shortcuts; clicking a fault opens its detail.

## 0.2.0

- Renamed the extension from `launch-control` to `basin` (now `aquila.basin`) so it can carry several
  panels. Uninstall `aquila.launch-control` when installing this.
- Fault Monitor panel: the live faults from `/state/fault_report`, with per-fault inspect and
  dismiss, as in the fault monitor TUI.
- Launch Control is unchanged apart from moving to `src/launch/` and sharing its theme with the
  new panel.

## 0.1.0

- Launch Control panel: per-system process table and status from `/state/launch/process_list`.
- Process controls (start, stop, restart) and system controls (launch, shutdown, hot reload,
  load config) through `/state/launch/<system_name>/request`.
