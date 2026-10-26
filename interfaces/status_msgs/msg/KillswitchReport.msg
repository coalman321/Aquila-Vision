# ========================================
# Constants
# ========================================

# The number of kill switches defined below
uint8 NUM_KILL_SWITCH_TYPES=4

# Note: Do not publish as physical kill switch. Reserved for firmware based ks
uint8 KILL_SWITCH_TYPE_PHYSICAL=0

# RQT Kill switch is for use in the controllers rqt plugin
uint8 KILL_SWITCH_TYPE_SOFTWARE=1

# The kill switch from the physical topside kill switch button
uint8 KILL_SWITCH_TYPE_REMOTE=2

# Debug kill switch should be used when publishing from command line or rqt manual publishing
# It is not recommended, but to avoid confusion this is implemented to allow manual publishing without conflicting
uint8 KILL_SWITCH_TYPE_DEBUG=3

# ========================================
# Message Definition
# ========================================

# The sender_id should uniquely identify the code running. This could be the node name because they should be unique
# This prevents from accidental re-enabling on kill from another node publishing as the same switch ID 
string sender_id

# A switch type defined above
uint8 kill_switch_type

# If the switch is in a kill state, stopping all active operations
bool switch_asserting_kill

# If the switch needs to periodically update in order to keep out of kill state
bool needs_heartbeat