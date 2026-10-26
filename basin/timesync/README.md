# timesync_ros

Keeps the clocks of a multi-computer robot in sync over
[PTP](https://www.linuxptp.org/). `sync_node` supervises a `ptp4l` and a
`phc2sys` child process and publishes the resulting sync state as
[`timesync_msgs/msg/SyncStatus`](../../interfaces/timesync_msgs/msg/SyncStatus.msg).

## Roles

One node in the graph is the grandmaster (`is_grandmaster:=true`); every other
computer runs a slave.

| | Grandmaster | Slave |
| --- | --- | --- |
| `ptp4l` | `--masterOnly 1`, started immediately | `--slaveOnly 1`, started (and restarted) by the tick loop |
| `phc2sys` | `-s CLOCK_REALTIME -c <iface>` per interface | `-c CLOCK_REALTIME -s <iface>` |
| Publishes | `sync/grandmaster` | `sync/slave` |
| Subscribes | — | `sync/grandmaster` |
| Interfaces | one or more | exactly one |

A slave moves `SLAVE_WAITING` → `SLAVE_SYNCING` when it first hears the
grandmaster. Each grandmaster message whose stamp differs from the local clock
by more than `max_sync_diff_ns` adds one to an out-of-sync counter, and each
close one takes one off (down to zero). The slave is `SLAVE_SYNCED` while that
counter is at most `out_of_sync_ticks`; once it exceeds it, the slave kills
`ptp4l` and drops back to `SLAVE_SYNCING`, and the tick loop restarts `ptp4l`
with the counter cleared. Note that a slave therefore reports `SLAVE_SYNCED`
almost as soon as it hears the grandmaster, and only loses it after sustained
misses.

## Parameters

| Parameter | Default | Meaning |
| --- | --- | --- |
| `is_grandmaster` | `false` | Run as grandmaster instead of slave |
| `interfaces` | `["eth0"]` | Network interfaces PTP runs on |
| `max_sync_diff_ns` | `2500000` | Largest grandmaster/local clock difference still counted as in sync |
| `out_of_sync_ticks` | `80` | Net out-of-sync observations tolerated before a synced slave restarts `ptp4l` |
| `ptp4l_path` | `/usr/sbin/ptp4l` | `ptp4l` binary to spawn (the Bazel build overrides this, see below) |
| `phc2sys_path` | `/usr/sbin/phc2sys` | `phc2sys` binary to spawn (the Bazel build overrides this, see below) |
| `use_software_stamps` | `false` | Declared but **not used** by the node yet |

The node refuses to start if either binary is missing, if `interfaces` is
empty, or if a slave is given more than one interface.

## Running

```bash
bazel build //basin/timesync/...

# Grandmaster / slave
bazel run //basin/timesync:sync_node -- --ros-args -p is_grandmaster:=true -p interfaces:="[eth0]"
bazel run //basin/timesync:sync_node -- --ros-args -p interfaces:="[eth0]"
```

`ptp4l` and `phc2sys` come from the BCR
[`linuxptp`](https://registry.bazel.build/modules/linuxptp) module, built from
source — nothing needs to be installed on the host. `:sync_node`'s `args`
point `ptp4l_path`/`phc2sys_path` at those builds; pass
`-p ptp4l_path:=/usr/sbin/ptp4l` (and likewise for `phc2sys`) to use the host's
instead.

They still have to be allowed to do their job: PTP needs a NIC with hardware
timestamping (or `linuxptp` configured for software timestamping) and
`phc2sys` needs permission to set the system clock, which in practice means
running as root or with `CAP_SYS_TIME`/`CAP_NET_RAW`/`CAP_NET_ADMIN`.

## Launching from a launch description

`:ament_index` (export name `timesync_ros`) publishes `sync_node`, `ptp4l` and
`phc2sys` together under `lib/timesync_ros/`, so a `.prod.py` can start it with
`Node(package="timesync_ros", executable="sync_node")` and set `ptp4l_path` and
`phc2sys_path` to the neighbouring files.
