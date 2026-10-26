std_msgs/Header header

# Unique name for the process
string process_name

# current mode of the ROS node
uint8 mode

uint8 MODE_STARTING = 0
uint8 MODE_RUNNING = 1
uint8 MODE_HALTING = 2
uint8 MODE_INVALID = 3