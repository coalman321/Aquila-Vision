# A single RTCM v3 correction message (or a batch of concatenated RTCM v3 frames), forwarded
# verbatim to a GNSS receiver's serial port to enable RTK. `data` carries the raw RTCM v3 bytes
# exactly as received from the correction source (NTRIP caster, base station radio link, etc.);
# no framing, parsing, or re-encoding is done downstream.

std_msgs/Header header

uint8[] data
