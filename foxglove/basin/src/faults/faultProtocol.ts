// Wire-level view of the fault topic: the shape of status_msgs/msg/FaultReport (with its FaultKey)
// and the bookkeeping basin/safety/scripts/fault_monitor_tui.py does on top of it. Kept free of
// React, and of imports, so it can be read (and run under plain node) on its own.

export const FAULT_REPORT_TOPIC = "/state/fault_report";

/** A fault whose expiry is closer than this is drawn as about to lapse. */
export const EXPIRING_SOON_SEC = 2;

export type FaultKey = string;

export type FaultEntry = {
  system: string;
  name: string;
  /** Fully qualified name of the node that raised it. */
  source: string;
  /** Start of the fault's current continuous period, in seconds; reporters keep it fixed while re-reporting. */
  raiseTime: number;
  /** Seconds; the fault is live until then, and stays live as long as reports keep extending it. */
  expirationTime: number;
  desc: string;
  /** Hidden by the user. Lives only in the panel: the reporters never learn about it. */
  dismissed: boolean;
};

// system and name are free text, so join them with a character neither can plausibly contain
// rather than risk ("a/b", "c") colliding with ("a", "b/c").
export function faultKey(system: string, name: string): FaultKey {
  return `${system}\u0000${name}`;
}

export function entryKey(entry: FaultEntry): FaultKey {
  return faultKey(entry.system, entry.name);
}

function asRecord(value: unknown): Record<string, unknown> | undefined {
  return typeof value === "object" && value != undefined
    ? (value as Record<string, unknown>)
    : undefined;
}

// builtin_interfaces/Time reaches the panel as { sec, nanosec }; older ROS-style times as { sec, nsec }.
function timeToSec(value: unknown): number {
  const record = asRecord(value);
  if (record == undefined) {
    return 0;
  }
  const sec = Number(record.sec ?? 0);
  const nanosec = Number(record.nanosec ?? record.nsec ?? 0);
  return Number.isFinite(sec) && Number.isFinite(nanosec) ? sec + nanosec * 1e-9 : 0;
}

export function parseFaultReport(message: unknown): FaultEntry | undefined {
  const record = asRecord(message);
  const key = asRecord(record?.key);
  if (record == undefined || key == undefined) {
    return undefined;
  }
  if (typeof key.fault_system !== "string" || typeof key.fault_name !== "string") {
    return undefined;
  }
  return {
    system: key.fault_system,
    name: key.fault_name,
    source: typeof record.source === "string" ? record.source : "",
    raiseTime: timeToSec(record.raise_time),
    expirationTime: timeToSec(record.expiration_time),
    desc: typeof record.fault_desc === "string" ? record.fault_desc : "",
    dismissed: false,
  };
}

export function isLive(entry: FaultEntry, nowSec: number): boolean {
  return entry.expirationTime > nowSec;
}

/**
 * Fold newly arrived reports into the known faults; the latest report for a key wins.
 *
 * A dismissal has to survive the reporter re-publishing a fault on every tick it stays active, so
 * it carries over to the new report while the fault it was made on is still live. Once that fault
 * had lapsed, a new report is a fresh occurrence and shows up again.
 */
export function applyFaultReports(
  previous: ReadonlyMap<FaultKey, FaultEntry>,
  reports: readonly FaultEntry[],
  nowSec: number,
): ReadonlyMap<FaultKey, FaultEntry> {
  const next = new Map(previous);
  for (const report of reports) {
    const key = entryKey(report);
    const existing = next.get(key);
    next.set(key, {
      ...report,
      dismissed: existing != undefined && isLive(existing, nowSec) && existing.dismissed,
    });
  }
  return next;
}

/** The live faults (plus the dismissed ones on request), ordered by system and then name. */
export function visibleFaults(
  faults: ReadonlyMap<FaultKey, FaultEntry>,
  nowSec: number,
  showDismissed: boolean,
): FaultEntry[] {
  return [...faults.values()]
    .filter((entry) => isLive(entry, nowSec) && (showDismissed || !entry.dismissed))
    .sort((a, b) => compareText(a.system, b.system) || compareText(a.name, b.name));
}

// Plain code-unit order, like the TUI's tuple sort: stable across locales, so rows never reshuffle.
function compareText(a: string, b: string): number {
  return a < b ? -1 : a > b ? 1 : 0;
}

export function setDismissed(
  faults: ReadonlyMap<FaultKey, FaultEntry>,
  keys: Iterable<FaultKey>,
  dismissed: boolean,
): ReadonlyMap<FaultKey, FaultEntry> {
  const next = new Map(faults);
  for (const key of keys) {
    const entry = next.get(key);
    if (entry != undefined) {
      next.set(key, { ...entry, dismissed });
    }
  }
  return next;
}

export function undismissAll(
  faults: ReadonlyMap<FaultKey, FaultEntry>,
): ReadonlyMap<FaultKey, FaultEntry> {
  return setDismissed(faults, faults.keys(), false);
}

export function formatDateTime(sec: number): string {
  return sec <= 0 ? "-" : new Date(sec * 1000).toLocaleString();
}

/** Like the TUI: 4.2s under a minute, 3m07s under an hour, 2h05m beyond; negative once past. */
export function formatDuration(deltaSec: number): string {
  const sign = deltaSec < 0 ? "-" : "";
  const delta = Math.abs(deltaSec);
  if (delta < 60) {
    return `${sign}${delta.toFixed(1)}s`;
  }
  const totalMinutes = Math.floor(delta / 60);
  const seconds = Math.floor(delta) % 60;
  if (totalMinutes < 60) {
    return `${sign}${totalMinutes}m${String(seconds).padStart(2, "0")}s`;
  }
  const hours = Math.floor(totalMinutes / 60);
  return `${sign}${hours}h${String(totalMinutes % 60).padStart(2, "0")}m`;
}
