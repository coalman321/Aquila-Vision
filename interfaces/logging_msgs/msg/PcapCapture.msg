# Network traffic captured by pcap_recorder around a trigger (a fault report). A capture is the
# recorder's in-memory buffer from before the trigger followed by live traffic until the capture
# window closes, split across as many messages as it takes.
#
# `data` is a complete pcap file (global header + packet records, exactly as `tcpdump -w` writes
# them), so any one message can be written to disk and opened on its own. To join a whole capture,
# keep the first message's data and append the others' minus their first 24 bytes (the header).
# `data` is empty only when a capture saw no packets at all.
std_msgs/Header header

# Unique per capture; every message of one capture carries it.
string capture_id

# Index of this message within the capture, from 0.
uint32 sequence

# True on the capture's last message, which may hold no packets.
bool is_last

# What started the capture: "<fault_system>/<fault_name>" for a fault report.
string trigger

# When the trigger arrived. Packets before it came from the pre-trigger buffer.
builtin_interfaces/Time trigger_time

# The interface tcpdump captured on.
string interface

# Packets in `data`, and the capture timestamps of the first and last of them (zero when there are
# none).
uint32 packet_count
builtin_interfaces/Time first_packet_time
builtin_interfaces/Time last_packet_time

uint8[] data
