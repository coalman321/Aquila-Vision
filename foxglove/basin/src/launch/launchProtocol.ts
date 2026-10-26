// Wire-level view of the launch node's interface: the constants and message shapes from
// interfaces/process_msgs (ProcessInfo, ProcessList, RemoteRequest), and the topic and service
// names basin/launch/core/src/launch_node.cpp publishes and serves them on. Kept free of React so
// it can be read (and tested) on its own.

export const PROCESS_LIST_TOPIC = "/state/launch/process_list";

// The node serves `/state/launch/<system_name>/request`, where <system_name> is the hostname its
// ProcessList reports, so the service name is derived from the message rather than configured.
export function requestServiceName(systemName: string): string {
  return `/state/launch/${systemName}/request`;
}

// process_msgs/msg/ProcessInfo PROC_STAT_*; the index is the label.
export const PROCESS_STATUS_LABELS = [
  "Inactive",
  "Starting",
  "Running",
  "Timeout",
  "Stopping",
  "Restarting",
  "Dead",
  "Invalid",
] as const;

export const ProcessStatus = {
  INACTIVE: 0,
  STARTING: 1,
  RUNNING: 2,
  TIMEOUT: 3,
  STOPPING: 4,
  RESTARTING: 5,
  DEAD: 6,
  INVALID: 7,
} as const;

// process_msgs/srv/RemoteRequest REQUEST_*
export const RequestId = {
  NONE: 0,
  START: 1,
  STOP: 2,
  RESTART: 3,
  LOAD_CONFIG: 4,
  LAUNCH: 5,
  SHUTDOWN: 6,
  HOTLOAD: 7,
} as const;

// process_msgs/srv/RemoteRequest RESPONSE_*
export const RESPONSE_OK = 0;

export type RemoteRequest = {
  request_id: number;
  id_argument: number;
  scenario_argument: string;
};

export function buildRequest(
  requestId: number,
  args: { processId?: number; scenario?: string } = {},
): RemoteRequest {
  return {
    request_id: requestId,
    id_argument: args.processId ?? 0,
    scenario_argument: args.scenario ?? "",
  };
}

export type RemoteResponse = {
  ok: boolean;
  code: number;
  message: string;
};

export function parseResponse(value: unknown): RemoteResponse | undefined {
  const record = asRecord(value);
  if (record == undefined || typeof record.resp_code !== "number") {
    return undefined;
  }
  return {
    ok: record.resp_code === RESPONSE_OK,
    code: record.resp_code,
    message: typeof record.response_msg === "string" ? record.response_msg : "",
  };
}

export type ProcessInfo = {
  processId: number;
  processName: string;
  status: number;
  restartCounter: number;
  pid: number;
  /** Fraction of one CPU, as published (0.25 is 25%). */
  cpuFraction: number;
  memoryBytes: number;
};

export type ProcessList = {
  systemName: string;
  /** Full path to the launch file the node has loaded. */
  activeConfig: string;
  processes: ProcessInfo[];
};

export type SystemState = ProcessList & {
  /** Wall-clock time this list arrived, for spotting a launch node that has gone quiet. */
  lastSeenMs: number;
};

function asRecord(value: unknown): Record<string, unknown> | undefined {
  return typeof value === "object" && value != undefined
    ? (value as Record<string, unknown>)
    : undefined;
}

// ROS 2 messages deserialize 64-bit integers (process_memory) as bigint, everything else as number.
function toNumber(value: unknown): number {
  return typeof value === "number" || typeof value === "bigint" ? Number(value) : 0;
}

export function parseProcessList(message: unknown): ProcessList | undefined {
  const record = asRecord(message);
  if (record == undefined || typeof record.system_name !== "string") {
    return undefined;
  }
  const rawProcesses = Array.isArray(record.processes) ? (record.processes as unknown[]) : [];
  const processes: ProcessInfo[] = [];
  for (const raw of rawProcesses) {
    const info = asRecord(raw);
    if (info == undefined) {
      continue;
    }
    processes.push({
      processId: toNumber(info.process_id),
      processName: typeof info.process_name === "string" ? info.process_name : "",
      status: toNumber(info.process_status),
      restartCounter: toNumber(info.restart_counter),
      pid: toNumber(info.local_pid),
      cpuFraction: toNumber(info.process_cpu_pct),
      memoryBytes: toNumber(info.process_memory),
    });
  }
  processes.sort((a, b) => a.processId - b.processId);
  return {
    systemName: record.system_name,
    activeConfig: typeof record.active_config === "string" ? record.active_config : "",
    processes,
  };
}

/** Fold newly arrived lists into the per-system state; the latest list for a system wins. */
export function applyProcessLists(
  previous: ReadonlyMap<string, SystemState>,
  lists: readonly ProcessList[],
  nowMs: number,
): ReadonlyMap<string, SystemState> {
  const next = new Map(previous);
  for (const list of lists) {
    next.set(list.systemName, { ...list, lastSeenMs: nowMs });
  }
  return next;
}

export function configName(activeConfig: string): string {
  return activeConfig.split("/").pop() ?? activeConfig;
}

export function statusLabel(status: number): string {
  return PROCESS_STATUS_LABELS[status] ?? "Invalid";
}
