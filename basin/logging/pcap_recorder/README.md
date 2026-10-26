# pcap_recorder

Keeps `tcpdump` running and the most recent network traffic in memory. When a fault is raised, it
publishes that buffer and then the live traffic for a set time on `pcap/capture`
([`logging_msgs/msg/PcapCapture`](../../../interfaces/logging_msgs/msg/PcapCapture.msg)), then
stops publishing and goes back to buffering. The [continuous logger](../continuous_logger/README.md)
can record that topic like any other, so a fault's network traffic ends up in the MCAP log next to
everything else.

## How it works

```
tcpdump -w - ──pipe──▶ parser ──▶ ring buffer (buffer.max_bytes / buffer.max_age)
                                     │
/state/fault_report ── match ──▶ trigger: drain buffer ─▶ publish queue ◀── live packets
                                                              │           (capture.duration)
                                                              ▼
                                                     pcap/capture, chunked to
                                                     capture.max_message_bytes
```

- **Buffering.** The node starts `tcpdump` with `-w -` and parses the pcap stream from its stdout.
  Every packet goes into a ring buffer. The oldest packets are dropped once the buffer holds more
  than `buffer.max_bytes`, or once they are older than `buffer.max_age`.
- **Triggering.** The node subscribes to `/state/fault_report`, where every
  `NodeCore::sendFaultReport` lands. A report triggers a capture when:
  - it matches `trigger.faults`, and
  - it hasn't expired, and
  - it is a new raise. Reporters re-send an active fault every tick with its original
    `raise_time`, so each fault period triggers once.

  The node's own reports never trigger it.
- **Publishing.** On a trigger, the node empties the buffer into a publish queue. Every packet
  captured over the next `capture.duration` seconds is queued too. Each tick (`core.tick_period`)
  publishes the queue as `PcapCapture` messages of at most `capture.max_message_bytes` each. The
  last message has `is_last: true`, and publishing then stops. A matching fault that arrives during
  a capture extends it to `capture.duration` from then; it doesn't start a second one.
- **Buffering continues during a capture**, so a fault shortly after one capture ends still has
  its own pre-fault history. That history can overlap the end of the previous capture.
- **tcpdump supervision.** If `tcpdump` exits (bad interface, missing permissions, bad filter),
  the node logs its stderr. It raises `pcap_recorder/tcpdump_not_running` every tick until
  `tcpdump` is back, and restarts it every `tcpdump.restart_delay` seconds.

### Reading a capture

Each message's `data` is a complete pcap file, so any single message opens in Wireshark as-is. A
whole capture is the first message's `data` followed by each later message's `data` minus its
24-byte global header:

```python
with open(f"{msgs[0].capture_id}.pcap", "wb") as f:
    for i, msg in enumerate(sorted(msgs, key=lambda m: m.sequence)):
        f.write(bytes(msg.data) if i == 0 else bytes(msg.data[24:]))
```

## Parameters

| Parameter | Default | Meaning |
| --- | --- | --- |
| `tcpdump.path` | `""` | `tcpdump` binary to run; empty means the bundled one (see below). The node refuses to start if it doesn't exist |
| `tcpdump.interface` | `any` | `-i` interface |
| `tcpdump.filter` | `""` | BPF filter expression, passed as `tcpdump`'s last argument |
| `tcpdump.snaplen` | `262144` | `-s` bytes kept per packet (`0` = tcpdump's default) |
| `tcpdump.immediate_mode` | `false` | Add `--immediate-mode` (deliver packets without kernel batching) |
| `tcpdump.extra_args` | `[]` | More arguments, placed after the fixed ones and before the filter |
| `tcpdump.restart_delay` | `5.0` | Seconds between restart attempts when `tcpdump` exits |
| `buffer.max_bytes` | `67108864` | Most packet bytes held in memory (64 MiB) |
| `buffer.max_age` | `30.0` | Seconds of history kept; `0` = limited by `buffer.max_bytes` only |
| `capture.duration` | `10.0` | Seconds of live traffic published after a trigger |
| `capture.max_message_bytes` | `1048576` | Largest `data` per message. A single bigger packet still goes, alone |
| `capture.queue_depth` | `100` | `pcap/capture` publisher history depth (reliable QoS) |
| `trigger.faults` | `["*"]` | Faults that trigger: `"*"`, `"<fault_system>/*"` or `"<fault_system>/<fault_name>"` |

The command line is `<tcpdump.path> -i <interface> -s <snaplen> -U -w - [--immediate-mode]
[extra_args...] [filter]`, and the node logs it at startup.
[config/pcap_recorder.yaml](config/pcap_recorder.yaml) is a commented example.

Size `buffer.max_bytes` for the traffic you expect. At 10 MB/s of captured traffic, 30 s of
history needs 300 MB, so the 64 MiB default would hold about 6 s. Use `tcpdump.filter` and a small
`tcpdump.snaplen` (headers only, e.g. `128`) to cut it down. A burst of messages on trigger is also
why `capture.queue_depth` defaults high. A subscriber that falls more than that far behind loses
messages; `sequence` shows where.

## Running

```bash
bazel build //basin/logging/pcap_recorder/...
bazel test //basin/logging/pcap_recorder/...

bazel run //basin/logging/pcap_recorder:pcap_recorder_node -- \
    --ros-args --params-file basin/logging/pcap_recorder/config/pcap_recorder.yaml

# Trigger it by hand: any fault report will do with the default trigger.faults
ros2 topic pub --once /state/fault_report status_msgs/msg/FaultReport \
    "{key: {fault_system: manual, fault_name: test}, expiration_time: {sec: 2000000000}}"
```

**tcpdump is bundled.** `@tcpdump` (BCR's 4.99.6, built against the workspace's libpcap) is
`data` of `:pcap_recorder`, so it's in the runfiles of the node, the tests and the
[release tarball](../../BUILD.bazel) (`basin/runfiles/tcpdump+/tcpdump`). With `tcpdump.path`
empty, the node finds it through the Bazel runfiles library
([src/bundled_tcpdump.cpp](src/bundled_tcpdump.cpp)). It looks in the runfiles that
`RUNFILES_DIR` names, which `bazel run`, `bazel test` and the tarball launchers all set, or else
next to the executable. If it can't find it, the node refuses to start rather than fall back to a
host copy. Set `tcpdump.path` to use a different tcpdump.

**Capturing needs privileges**, and a tarball can't carry file capabilities. Run the node as
root, or set them on the bundled binary after unpacking:

```bash
sudo setcap cap_net_raw,cap_net_admin=eip basin/runfiles/tcpdump+/tcpdump
```

Without them, `tcpdump` exits at once, and the node reports `tcpdump_not_running` with tcpdump's
own error in the description.

## Deployment

- **Standalone:** `:pcap_recorder_node`, which is also in the [release tarball](../../BUILD.bazel)
  as `basin/bin/pcap_recorder_node`.
- **Component:** `basin::pcap_recorder::PcapRecorderNode`, registered under the `pcap_recorder`
  ament package (`:ament_index`). A `basin_container` config loads it with
  `package_name: pcap_recorder`. `:ament_index` also publishes `config/`, and puts
  `pcap_recorder_node` (and `tcpdump`) at `lib/pcap_recorder/` for a launch description's
  `Node(package="pcap_recorder", executable="pcap_recorder_node")`.

`tcpdump` runs under a `basin::process::ProcessController` ([process_lib](../../core/process_lib/README.md))
in `OutputMode::PIPE`, with a parent-death signal of SIGTERM, so it doesn't outlive the node even
when the node is killed. A reader thread polls its stdout and stderr pipes.

## Tests

`:fake_tcpdump` stands in for `tcpdump`: it streams a pcap of numbered packets, so the tests need
neither a real `tcpdump` nor capture privileges.

| Target | Covers |
| --- | --- |
| `:pcap_stream_test` | Parser (split reads, byte orders, ns timestamps, bad input), ring buffer limits, chunking |
| `:pcap_recorder_params_test` | `trigger.faults` matching, the tcpdump command line |
| `:pcap_recorder_node_test` | The node end to end: buffer age, a capture with no gaps or duplicates, no retrigger on repeats, filters |
| `:pcap_recorder_component_test` | Loading in `basin_container` through the ament index, clean shutdown of the child |
