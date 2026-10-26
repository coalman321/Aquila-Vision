# Published by the logger on every tick of its node (core.tick_period, 10 Hz by default)
builtin_interfaces/Time stamp

# Utilization of the filesystem the log directory is on. Old log files are rotated away once
# disk_usage_percent reaches disk_max_usage_percent, except those a snapshot protects.
string log_path
float64 disk_usage_percent
float64 disk_max_usage_percent
float64 disk_warning_threshold_percent
uint64 disk_total_bytes
uint64 disk_free_bytes

# The events the logger is configured to snapshot on
EventStatus[] events

# Every snapshot that is running or pending, then the most recently started completed ones.
# total_snapshot_count is how many exist in all, so a longer history is knowable but not sent.
SnapshotStatus[] snapshots
uint32 total_snapshot_count
