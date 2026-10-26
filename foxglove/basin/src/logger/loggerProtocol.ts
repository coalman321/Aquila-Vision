// Wire-level view of the continuous logger's interface: the shapes of logging_msgs/msg/LoggerStatus,
// logging_msgs/srv/SnapshotRequest and logging_msgs/msg/LogMetadata, the topic and service names
// basin/logging/continuous_logger serves them on, and the parsing and formatting the panel does on
// top of them. The logger's status doubles as its liveness: a logger that is sending it is up. Kept
// free of React so it can be read (and tested) on its own.

export const STATUS_TOPIC = "/log/logger_status";
export const METADATA_TOPIC = "/log/metadata";
export const METADATA_SCHEMA = "logging_msgs/msg/LogMetadata";
export const SNAPSHOT_SERVICE = "/log/snapshot";

// logging_msgs/srv/SnapshotRequest REQUEST_*
export const RequestId = {
  NONE: 0,
  START: 1,
  STOP: 2,
} as const;

// logging_msgs/srv/SnapshotRequest RESPONSE_*. Stopping a snapshot that is not running (unknown, or
// already stopped) answers INVALID_NAME; starting one whose name was ever used answers
// SNAPSHOT_EXISTS.
export const ResponseCode = {
  OK: 0,
  REQ_INVALID: 1,
  INVALID_NAME: 2,
  SNAPSHOT_ACTIVE: 3,
  SNAPSHOT_EXISTS: 4,
} as const;

export type SnapshotRequest = {
  request_id: number;
  snapshot_name: string;
  description: string;
  tags: string[];
};

export function buildSnapshotRequest(
  requestId: number,
  args: { name?: string; description?: string; tags?: string[] } = {},
): SnapshotRequest {
  return {
    request_id: requestId,
    snapshot_name: args.name ?? "",
    description: args.description ?? "",
    tags: args.tags ?? [],
  };
}

export type SnapshotResponse = {
  ok: boolean;
  code: number;
  message: string;
};

function asRecord(value: unknown): Record<string, unknown> | undefined {
  return typeof value === "object" && value != undefined
    ? (value as Record<string, unknown>)
    : undefined;
}

export function parseSnapshotResponse(value: unknown): SnapshotResponse | undefined {
  const record = asRecord(value);
  if (record == undefined || typeof record.resp_code !== "number") {
    return undefined;
  }
  return {
    ok: record.resp_code === ResponseCode.OK,
    code: record.resp_code,
    message: typeof record.response_msg === "string" ? record.response_msg : "",
  };
}

/** "a, b,, c" -> ["a", "b", "c"] */
export function parseTags(text: string): string[] {
  return text
    .split(",")
    .map((tag) => tag.trim())
    .filter((tag) => tag !== "");
}

// logging_msgs/msg/SnapshotStatus STATE_*; the index is the label.
export const SNAPSHOT_STATE_LABELS = ["Running", "Pending", "Complete"] as const;

export const SnapshotState = {
  /** A manual snapshot that has not been stopped. */
  RUNNING: 0,
  /** An event snapshot still recording its time_after window. */
  PENDING: 1,
  COMPLETE: 2,
} as const;

export type SnapshotEntry = {
  id: string;
  /** "manual", or "event-<event_name>". */
  trigger: string;
  description: string;
  tags: string[];
  /** Epoch seconds. */
  startTime: number;
  /** Epoch seconds; undefined until the snapshot has finished. */
  endTime: number | undefined;
  state: number;
};

/** An event the logger is configured to turn into a snapshot. */
export type ConfiguredEvent = {
  name: string;
  timeBefore: number;
  timeAfter: number;
  tags: string[];
  description: string;
  /** Snapshots it has triggered, counting those from before the logger last restarted. */
  triggerCount: number;
};

export type LoggerStatus = {
  logPath: string;
  diskUsagePercent: number;
  diskMaxUsagePercent: number;
  diskWarningThresholdPercent: number;
  diskTotalBytes: number;
  diskFreeBytes: number;
  events: ConfiguredEvent[];
  /** Everything running or pending, then the most recently started completed ones. */
  snapshots: SnapshotEntry[];
  /** How many snapshots exist in all, which can exceed `snapshots.length`. */
  totalSnapshotCount: number;
  /** Wall-clock time this status arrived, for spotting a logger that has stopped publishing. */
  lastSeenMs: number;
};

// ROS 2 messages deserialize 64-bit integers (the byte counts, the snapshot times) as bigint,
// everything else as number.
function toNumber(value: unknown): number {
  return typeof value === "number" || typeof value === "bigint" ? Number(value) : 0;
}

function toText(value: unknown): string {
  return typeof value === "string" ? value : "";
}

function toTexts(value: unknown): string[] {
  return Array.isArray(value) ? (value as unknown[]).filter((item): item is string => typeof item === "string") : [];
}

function toRecords(value: unknown): Record<string, unknown>[] {
  return Array.isArray(value)
    ? (value as unknown[]).flatMap((item) => {
        const record = asRecord(item);
        return record == undefined ? [] : [record];
      })
    : [];
}

export function parseLoggerStatus(message: unknown, arrivedMs: number): LoggerStatus | undefined {
  const record = asRecord(message);
  if (record == undefined || typeof record.log_path !== "string") {
    return undefined;
  }
  return {
    logPath: record.log_path,
    diskUsagePercent: toNumber(record.disk_usage_percent),
    diskMaxUsagePercent: toNumber(record.disk_max_usage_percent),
    diskWarningThresholdPercent: toNumber(record.disk_warning_threshold_percent),
    diskTotalBytes: toNumber(record.disk_total_bytes),
    diskFreeBytes: toNumber(record.disk_free_bytes),
    events: toRecords(record.events).map((event) => ({
      name: toText(event.event_name),
      timeBefore: toNumber(event.time_before),
      timeAfter: toNumber(event.time_after),
      tags: toTexts(event.tags),
      description: toText(event.description),
      triggerCount: toNumber(event.trigger_count),
    })),
    snapshots: toRecords(record.snapshots).map((snapshot) => {
      const endTime = toNumber(snapshot.end_time);
      return {
        id: toText(snapshot.id),
        trigger: toText(snapshot.trigger),
        description: toText(snapshot.description),
        tags: toTexts(snapshot.tags),
        startTime: toNumber(snapshot.start_time),
        // -1 until the snapshot has finished
        endTime: endTime < 0 ? undefined : endTime,
        state: toNumber(snapshot.state),
      };
    }),
    totalSnapshotCount: toNumber(record.total_snapshot_count),
    lastSeenMs: arrivedMs,
  };
}

export function snapshotStateLabel(state: number): string {
  return SNAPSHOT_STATE_LABELS[state] ?? "Unknown";
}

export type DiskLevel = "ok" | "warn" | "bad";

/** Past the warning threshold the logger logs a warning; at the maximum it starts rotating logs away. */
export function diskLevel(status: LoggerStatus): DiskLevel {
  if (status.diskUsagePercent >= status.diskMaxUsagePercent) {
    return "bad";
  }
  return status.diskUsagePercent >= status.diskWarningThresholdPercent ? "warn" : "ok";
}

/** 12.3 GB: decimal units, like the disk size a filesystem is sold at. */
export function formatBytes(bytes: number): string {
  const units = ["B", "kB", "MB", "GB", "TB"];
  let value = bytes;
  let unit = 0;
  while (value >= 1000 && unit < units.length - 1) {
    value /= 1000;
    unit += 1;
  }
  return `${unit === 0 ? value.toFixed(0) : value.toFixed(1)} ${units[unit] ?? "B"}`;
}

/** One message seen on /log/metadata: an event (timed) or a plain metadata entry. */
export type MetadataEvent = {
  id: number;
  /** Data source receive time, so a recording shows when the event was logged, not when it was played. */
  atMs: number;
  key: string;
  timed: boolean;
  yaml: string;
};

const MAX_EVENTS = 30;

export function parseMetadataEvent(
  message: unknown,
  atMs: number,
  id: number,
): MetadataEvent | undefined {
  const record = asRecord(message);
  if (record == undefined || typeof record.key_name !== "string") {
    return undefined;
  }
  return {
    id,
    atMs,
    key: record.key_name,
    timed: record.timed_event === true,
    yaml: typeof record.yaml_metadata === "string" ? record.yaml_metadata : "",
  };
}

/** Newest first. `incoming` is in arrival order. */
export function addEvents(
  previous: readonly MetadataEvent[],
  incoming: readonly MetadataEvent[],
): readonly MetadataEvent[] {
  return [...incoming].reverse().concat(previous).slice(0, MAX_EVENTS);
}

// The header stamp stays zero: for a timed event the logger then stamps it with its own clock, so
// the snapshot window it opens around the event (time_before / time_after) is placed on the robot's
// time, however far off the browser's clock is.
export function buildMetadataMessage(args: { key: string; yaml: string; timed: boolean }): unknown {
  return {
    header: { stamp: { sec: 0, nanosec: 0 }, frame_id: "" },
    timed_event: args.timed,
    key_name: args.key,
    yaml_metadata: args.yaml,
  };
}

export function timeToMs(time: { sec: number; nsec: number }): number {
  return time.sec * 1000 + time.nsec / 1e6;
}
