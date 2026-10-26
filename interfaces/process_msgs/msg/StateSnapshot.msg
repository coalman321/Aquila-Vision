# A `{sequence, sim_time, serialized state}` snapshot of a node's internal state, taken by
# basin::node_core::determinism::ExecutionRecorder::maybeSnapshot and published live by
# DeterministicNode. The replay harness finds the snapshot with the largest `sequence` <= a
# target sequence and restores from it before walking the dispatch log forward.

# Sequence number this snapshot was taken at
uint64 sequence

# Simulated/ROS time the snapshot was taken at
builtin_interfaces/Time sim_time

# Node state, serialized as JSON by basin::node_core::determinism::StateArchive
string state_json
