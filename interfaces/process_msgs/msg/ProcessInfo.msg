# Unique process ID in the launch config
uint32 process_id

# Unique name for the process
string process_name

# Current lifecycle status of the process
uint8 process_status

uint8 PROC_STAT_INACTIVE = 0
uint8 PROC_STAT_STARTING = 1
uint8 PROC_STAT_RUNNING = 2
uint8 PROC_STAT_TIMEOUT = 3
uint8 PROC_STAT_STOPPING = 4
uint8 PROC_STAT_RESTARTING = 5
uint8 PROC_STAT_DEAD = 6
uint8 PROC_STAT_INVALID = 7

# the number of times this process has automatically restarted
uint32 restart_counter

# Current PID of the process on the local machine
int32 local_pid

# Percent of CPU process is currently using
float32 process_cpu_pct

# Amount of memory in bytes the process is using
uint64 process_memory
