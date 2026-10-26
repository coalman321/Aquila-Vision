# Provenance for a message published while handling dispatch `producing_sequence` -- ties a
# received message back to the callback execution that produced it. Needed by Phase 2/3
# (multi-node/distributed replay); published now because it's the provenance hook a later
# phase needs and it's cheap to add now, expensive to retrofit once nodes are wired up
# without it. Published live by DeterministicNode's ExecutionRecorder.

# Sequence number of the dispatch that produced this publish
uint64 producing_sequence

# Topic the message was published on
string topic

# RMW GID of the publisher that sent it
uint8[16] writer_gid

# Publish count for this writer so far, 0-indexed
uint64 per_writer_ordinal
