import { Immutable, PanelExtensionContext, RenderState } from "@foxglove/extension";
import {
  FormEvent,
  ReactElement,
  useCallback,
  useEffect,
  useId,
  useLayoutEffect,
  useRef,
  useState,
} from "react";
import { createRoot } from "react-dom/client";

import {
  LoggerStatus,
  METADATA_SCHEMA,
  METADATA_TOPIC,
  MetadataEvent,
  RequestId,
  SNAPSHOT_SERVICE,
  STATUS_TOPIC,
  SnapshotEntry,
  SnapshotRequest,
  SnapshotResponse,
  SnapshotState,
  addEvents,
  buildMetadataMessage,
  buildSnapshotRequest,
  diskLevel,
  formatBytes,
  parseLoggerStatus,
  parseMetadataEvent,
  parseSnapshotResponse,
  parseTags,
  snapshotStateLabel,
  timeToMs,
} from "./loggerProtocol";
import { themeStyles } from "../common/theme";
import { styles } from "./loggerStyles";

// The status goes out every tick (10 Hz by default); this many missed seconds means the logger has
// stopped sending it.
const STATUS_STALE_AFTER_MS = 5000;
const MAX_LOG_ENTRIES = 8;

type LogEntry = {
  id: number;
  atMs: number;
  action: string;
  ok: boolean;
  message: string;
};

function snapshotBadgeClass(state: number): string {
  switch (state) {
    case SnapshotState.RUNNING:
      return "ok";
    case SnapshotState.PENDING:
      return "warn";
    default:
      return "idle";
  }
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

function formatTime(ms: number): string {
  return new Date(ms).toLocaleTimeString();
}

/** Snapshot times are epoch seconds; they outlive the day they were made, so the date is shown. */
function formatEpoch(sec: number | undefined): string {
  return sec == undefined ? "–" : new Date(sec * 1000).toLocaleString();
}

// What a section that needs the logger's status shows until one arrives.
function WaitingForStatus(props: { advertised: boolean | undefined }): ReactElement {
  return (
    <div className="cl-empty">
      {props.advertised === false ? (
        <>
          <code>{STATUS_TOPIC}</code> isn&apos;t advertised. Check that the logger is running and up to date.
        </>
      ) : (
        <>
          Waiting for a status on <code>{STATUS_TOPIC}</code>…
        </>
      )}
    </div>
  );
}

function SnapshotRow(props: {
  snapshot: SnapshotEntry;
  busy: boolean;
  onStop: (name: string) => void;
}): ReactElement {
  const { snapshot, busy, onStop } = props;
  return (
    <tr>
      <td className="name" title={snapshot.description}>
        {snapshot.id}
      </td>
      <td>
        <span className={`bp-badge ${snapshotBadgeClass(snapshot.state)}`}>{snapshotStateLabel(snapshot.state)}</span>
      </td>
      <td>{snapshot.trigger}</td>
      <td className="when">{formatEpoch(snapshot.startTime)}</td>
      <td className="when">{formatEpoch(snapshot.endTime)}</td>
      <td className="tags">{snapshot.tags.join(", ")}</td>
      <td>
        {/* A pending event snapshot ends itself when its time_after window closes. */}
        {snapshot.state === SnapshotState.RUNNING && (
          <button
            type="button"
            className="bp-btn small"
            disabled={busy}
            onClick={() => {
              onStop(snapshot.id);
            }}
          >
            Stop
          </button>
        )}
      </td>
    </tr>
  );
}

function DiskUsage(props: { status: LoggerStatus }): ReactElement {
  const { status } = props;
  const percent = Math.min(100, Math.max(0, status.diskUsagePercent));
  return (
    <div className="cl-disk">
      <div
        className="cl-bar"
        role="progressbar"
        aria-label="Disk usage"
        aria-valuemin={0}
        aria-valuemax={100}
        aria-valuenow={Math.round(percent)}
      >
        <div className={`cl-bar-fill ${diskLevel(status)}`} style={{ width: `${percent}%` }} />
      </div>
      <div className="cl-disk-text">
        <span className="cl-disk-percent">{status.diskUsagePercent.toFixed(1)}% used</span>
        <span className="cl-muted">
          {formatBytes(status.diskFreeBytes)} free of {formatBytes(status.diskTotalBytes)}
        </span>
      </div>
      <div className="cl-muted cl-path" title="The filesystem this directory is on">
        {status.logPath}
      </div>
    </div>
  );
}

function ContinuousLoggerPanel({ context }: { context: PanelExtensionContext }): ReactElement {
  const [renderDone, setRenderDone] = useState<(() => void) | undefined>();
  const [colorScheme, setColorScheme] = useState<"dark" | "light">("dark");
  const [statusAdvertised, setStatusAdvertised] = useState<boolean | undefined>();
  const [status, setStatus] = useState<LoggerStatus | undefined>();
  const [nowMs, setNowMs] = useState(() => Date.now());
  const [events, setEvents] = useState<readonly MetadataEvent[]>([]);
  const [busy, setBusy] = useState(false);
  const [log, setLog] = useState<readonly LogEntry[]>([]);
  const nextId = useRef(0);
  const eventNamesId = useId();

  const [snapshotName, setSnapshotName] = useState("");
  const [snapshotDescription, setSnapshotDescription] = useState("");
  const [snapshotTags, setSnapshotTags] = useState("");
  const [eventKey, setEventKey] = useState("");
  const [eventYaml, setEventYaml] = useState("");
  const [eventTimed, setEventTimed] = useState(true);

  useLayoutEffect(() => {
    context.onRender = (renderState: Immutable<RenderState>, done) => {
      setRenderDone(() => done);
      setColorScheme(renderState.colorScheme ?? "dark");

      if (renderState.topics) {
        setStatusAdvertised(renderState.topics.some((topic) => topic.name === STATUS_TOPIC));
      }

      if (renderState.currentFrame) {
        const arrivedMs = Date.now();
        let latestStatus: LoggerStatus | undefined;
        const incoming: MetadataEvent[] = [];
        for (const event of renderState.currentFrame) {
          if (event.topic === STATUS_TOPIC) {
            latestStatus = parseLoggerStatus(event.message, arrivedMs) ?? latestStatus;
          } else if (event.topic === METADATA_TOPIC) {
            const parsed = parseMetadataEvent(event.message, timeToMs(event.receiveTime), nextId.current);
            if (parsed) {
              nextId.current += 1;
              incoming.push(parsed);
            }
          }
        }
        if (latestStatus) {
          setStatus(latestStatus);
        }
        if (incoming.length > 0) {
          setEvents((previous) => addEvents(previous, incoming));
        }
      }
    };

    context.watch("colorScheme");
    context.watch("topics");
    context.watch("currentFrame");
    context.subscribe([{ topic: STATUS_TOPIC }, { topic: METADATA_TOPIC }]);
    // Publishing an event needs the topic advertised first; data sources that can't publish leave
    // these undefined, and the form is disabled below.
    context.advertise?.(METADATA_TOPIC, METADATA_SCHEMA);

    return () => {
      context.onRender = undefined;
      context.unadvertise?.(METADATA_TOPIC);
    };
  }, [context]);

  // The host waits for this after every render before delivering the next one.
  useEffect(() => {
    renderDone?.();
  }, [renderDone]);

  // Re-evaluate staleness even when no messages arrive.
  useEffect(() => {
    const timer = setInterval(() => {
      setNowMs(Date.now());
    }, 1000);
    return () => {
      clearInterval(timer);
    };
  }, []);

  const pushLog = useCallback((action: string, ok: boolean, message: string) => {
    const entry: LogEntry = { id: nextId.current++, atMs: Date.now(), action, ok, message };
    setLog((previous) => [entry, ...previous].slice(0, MAX_LOG_ENTRIES));
  }, []);

  const call = useCallback(
    (action: string, request: SnapshotRequest, onResponse?: (response: SnapshotResponse) => void) => {
      if (context.callService == undefined) {
        pushLog(action, false, "This data source does not support service calls");
        return;
      }
      setBusy(true);
      context
        .callService(SNAPSHOT_SERVICE, request)
        .then((raw) => {
          const response = parseSnapshotResponse(raw);
          if (response) {
            pushLog(action, response.ok, response.message || `response code ${response.code}`);
            onResponse?.(response);
          } else {
            pushLog(action, false, "Unexpected response from the logger");
          }
        })
        .catch((error: unknown) => {
          pushLog(action, false, errorMessage(error));
        })
        .finally(() => {
          setBusy(false);
        });
    },
    [context, pushLog],
  );

  // The snapshot list refreshes with the next status, so a start or stop shows up within a tick.
  const startSnapshot = (event: FormEvent): void => {
    event.preventDefault();
    const name = snapshotName.trim();
    if (name === "") {
      return;
    }
    const description = snapshotDescription.trim();
    const tags = parseTags(snapshotTags);
    call(`Start ${name}`, buildSnapshotRequest(RequestId.START, { name, description, tags }), (response) => {
      if (response.ok) {
        setSnapshotName("");
        setSnapshotDescription("");
        setSnapshotTags("");
      }
    });
  };

  const stopSnapshot = (name: string): void => {
    call(`Stop ${name}`, buildSnapshotRequest(RequestId.STOP, { name }));
  };

  const sendEvent = (event: FormEvent): void => {
    event.preventDefault();
    const key = eventKey.trim();
    if (key === "" || context.publish == undefined) {
      return;
    }
    try {
      context.publish(METADATA_TOPIC, buildMetadataMessage({ key, yaml: eventYaml, timed: eventTimed }));
      pushLog(`${eventTimed ? "Event" : "Metadata"} ${key}`, true, `published on ${METADATA_TOPIC}`);
      setEventKey("");
      setEventYaml("");
    } catch (error) {
      pushLog(`Event ${key}`, false, errorMessage(error));
    }
  };

  // Service calls need a live connection; staleness only means something there too, since a
  // paused or recorded source stops delivering the status without the logger being gone.
  const live = context.callService != undefined;
  const canPublish = context.publish != undefined;
  const statusStale = live && status != undefined && nowMs - status.lastSeenMs > STATUS_STALE_AFTER_MS;
  const listedSnapshots = status?.snapshots.length ?? 0;

  return (
    <div className="basin-panel" data-theme={colorScheme}>
      <style>{themeStyles}</style>
      <style>{styles}</style>

      {!live && (
        <div className="cl-readonly">
          Read-only: this data source can&apos;t call services, so snapshot control is disabled.
        </div>
      )}

      <section className="cl-card">
        <div className="cl-head">
          <span className="cl-title">Continuous Logger</span>
          <span className="cl-spacer" />
          {status == undefined ? (
            <span className="cl-muted">
              {statusAdvertised === false ? `${STATUS_TOPIC} isn't advertised` : "Waiting for a status…"}
            </span>
          ) : (
            <span
              className={`bp-badge ${statusStale ? "bad" : "ok"}`}
              title={
                statusStale
                  ? `Nothing on ${STATUS_TOPIC} for ${STATUS_STALE_AFTER_MS / 1000} s: what is shown below may be out of date`
                  : `Receiving ${STATUS_TOPIC}`
              }
            >
              {statusStale ? "Stalled" : "Running"}
            </span>
          )}
          <button
            type="button"
            className="bp-btn small"
            title="Call the snapshot service with an empty request; the logger answers PONG"
            disabled={!live || busy}
            onClick={() => {
              call("Ping", buildSnapshotRequest(RequestId.NONE));
            }}
          >
            Ping
          </button>
        </div>
      </section>

      <section className="cl-card">
        <div className="cl-section-title">Disk</div>
        {status == undefined ? <WaitingForStatus advertised={statusAdvertised} /> : <DiskUsage status={status} />}
      </section>

      <section className="cl-card">
        <div className="cl-section-title">Snapshots</div>
        <form className="cl-form" onSubmit={startSnapshot}>
          <input
            className="cl-input"
            value={snapshotName}
            placeholder="Name"
            aria-label="Snapshot name"
            onChange={(event) => {
              setSnapshotName(event.target.value);
            }}
          />
          <input
            className="cl-input wide"
            value={snapshotDescription}
            placeholder="Description (optional)"
            aria-label="Snapshot description"
            onChange={(event) => {
              setSnapshotDescription(event.target.value);
            }}
          />
          <input
            className="cl-input"
            value={snapshotTags}
            placeholder="Tags, comma separated"
            aria-label="Snapshot tags"
            onChange={(event) => {
              setSnapshotTags(event.target.value);
            }}
          />
          <button type="submit" className="bp-btn" disabled={!live || busy || snapshotName.trim() === ""}>
            Start
          </button>
        </form>
        {status == undefined ? (
          <WaitingForStatus advertised={statusAdvertised} />
        ) : status.snapshots.length === 0 ? (
          <div className="cl-empty">The logger holds no snapshots.</div>
        ) : (
          <div className="cl-table-wrap">
            <table className="cl-table">
              <thead>
                <tr>
                  <th>Name</th>
                  <th>Status</th>
                  <th>Trigger</th>
                  <th>Started</th>
                  <th>Ended</th>
                  <th>Tags</th>
                  <th />
                </tr>
              </thead>
              <tbody>
                {status.snapshots.map((snapshot) => (
                  <SnapshotRow key={snapshot.id} snapshot={snapshot} busy={!live || busy} onStop={stopSnapshot} />
                ))}
              </tbody>
            </table>
          </div>
        )}
        {status != undefined && status.totalSnapshotCount > listedSnapshots && (
          <div className="cl-hint">
            Showing {listedSnapshots} of {status.totalSnapshotCount} snapshots: everything running or pending,
            then the newest completed.
          </div>
        )}
      </section>

      <section className="cl-card">
        <div className="cl-section-title">Events</div>
        {status == undefined ? (
          <WaitingForStatus advertised={statusAdvertised} />
        ) : status.events.length === 0 ? (
          <div className="cl-empty">No event snapshots are configured.</div>
        ) : (
          <div className="cl-table-wrap">
            <table className="cl-table">
              <thead>
                <tr>
                  <th>Event</th>
                  <th className="num">Before (s)</th>
                  <th className="num">After (s)</th>
                  <th>Tags</th>
                  <th className="num">Triggered</th>
                </tr>
              </thead>
              <tbody>
                {status.events.map((event) => (
                  <tr key={event.name}>
                    <td className="name" title={event.description}>
                      {event.name}
                    </td>
                    <td className="num">{event.timeBefore}</td>
                    <td className="num">{event.timeAfter}</td>
                    <td className="tags">{event.tags.join(", ")}</td>
                    <td className="num">{event.triggerCount}</td>
                  </tr>
                ))}
              </tbody>
            </table>
          </div>
        )}
        <form className="cl-form" onSubmit={sendEvent}>
          <input
            className="cl-input"
            list={eventNamesId}
            value={eventKey}
            placeholder="Event name"
            aria-label="Event name"
            onChange={(event) => {
              setEventKey(event.target.value);
            }}
          />
          <datalist id={eventNamesId}>
            {status?.events.map((event) => <option key={event.name} value={event.name} />)}
          </datalist>
          <input
            className="cl-input wide"
            value={eventYaml}
            placeholder="YAML metadata (optional)"
            aria-label="Event YAML metadata"
            onChange={(event) => {
              setEventYaml(event.target.value);
            }}
          />
          <label className="cl-check">
            <input
              type="checkbox"
              checked={eventTimed}
              onChange={(event) => {
                setEventTimed(event.target.checked);
              }}
            />
            Timed
          </label>
          <button type="submit" className="bp-btn" disabled={!canPublish || eventKey.trim() === ""}>
            Send
          </button>
        </form>
        {events.length === 0 ? (
          <div className="cl-empty">
            {canPublish ? "" : "This data source can't publish, so events can't be sent. "}
            No events seen on <code>{METADATA_TOPIC}</code>.
          </div>
        ) : (
          <ul className="cl-events">
            {events.map((event) => (
              <li key={event.id}>
                <span className="time">{formatTime(event.atMs)}</span>
                <span className="key">{event.key}</span>
                {event.timed && <span className="bp-badge idle">timed</span>}
                <span className="yaml" title={event.yaml}>
                  {event.yaml}
                </span>
              </li>
            ))}
          </ul>
        )}
      </section>

      {log.length > 0 && (
        <ul className="cl-log">
          {log.map((entry) => (
            <li key={entry.id}>
              <span className="time">{new Date(entry.atMs).toLocaleTimeString()}</span>
              <span className={entry.ok ? "ok" : "bad"}>
                {entry.action} — {entry.message}
              </span>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}

export function initContinuousLoggerPanel(context: PanelExtensionContext): () => void {
  const root = createRoot(context.panelElement);
  root.render(<ContinuousLoggerPanel context={context} />);
  return () => {
    root.unmount();
  };
}
