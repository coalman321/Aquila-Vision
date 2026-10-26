string id

# "manual" for a snapshot started through the snapshot service, "event-<event_name>" for one an
# event triggered
string trigger
string description
string[] tags

# Epoch seconds. end_time is -1 until the snapshot has finished.
int64 start_time
int64 end_time

uint8 state

uint8 STATE_RUNNING = 0 # a manual snapshot that has not been stopped
uint8 STATE_PENDING = 1 # an event snapshot still recording its time_after window
uint8 STATE_COMPLETE = 2 # stopped: its time span is protected from log rotation
