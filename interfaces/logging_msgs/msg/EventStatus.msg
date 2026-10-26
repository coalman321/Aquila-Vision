# One event the logger is configured to turn into a snapshot: an `event_snapshots.<key>` entry of its
# parameters. A timed LogMetadata whose key_name equals event_name starts a snapshot around it.
string event_name

# Seconds of log before and after the event that the snapshot covers
float64 time_before
float64 time_after

# Tags and description applied to the snapshots this event creates
string[] tags
string description

# How many snapshots this event has triggered, counting those from before the logger last
# restarted. Snapshot IDs are event_<event_name>_<n>, so this is also the n the next one gets.
uint64 trigger_count
