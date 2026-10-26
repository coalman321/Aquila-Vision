# Hostname of the system publishing this message
string hostname

# The current rclcpp clock time
builtin_interfaces/Time stamp

# The current status of the sync
uint8 status

# status enum values
uint8 STATUS_GRANDMASTER = 0 # Grandmaster is always in sync as it is the master clock
uint8 STATUS_SLAVE_WAITING = 1 # Slave waiting is a slave clock that has not yet found a grandmster
uint8 STATUS_SLAVE_SYNCING = 2 # Slave Syncing is a slave clock that has found a GM clock but is still syncing
uint8 STATUS_SLAVE_SYNCED = 3 # Slave Synced is a slave clock that has found andsynchronized to a GM clock