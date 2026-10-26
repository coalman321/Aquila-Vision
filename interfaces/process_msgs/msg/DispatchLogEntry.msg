# One recorded executor dispatch: the executor ran `entity_name` (a `kind`) as the
# `sequence`-th callback of the run, at `sim_time`. Published live by
# basin::node_core::determinism::DeterministicNode's ExecutionRecorder -- persisted only if
# something (e.g. `ros2 bag record`) is listening on this topic.

# Global sequence number for this dispatch, monotonically increasing per node
uint64 sequence

# Kind of entity that was dispatched
uint8 kind

uint8 KIND_TIMER = 0
uint8 KIND_SUBSCRIPTION = 1
uint8 KIND_SERVICE = 2
uint8 KIND_CLIENT = 3
uint8 KIND_WAITABLE = 4

# Name identifying the specific entity: topic name, service name, or a name registered for a
# timer via ExecutionRecorder::registerTimerName (timers carry no name of their own)
string entity_name

# Simulated/ROS time the dispatch occurred at
builtin_interfaces/Time sim_time
