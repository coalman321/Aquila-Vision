import { Immutable, PanelExtensionContext, RenderState } from "@foxglove/extension";
import {
  FormEvent,
  ReactElement,
  useCallback,
  useEffect,
  useLayoutEffect,
  useRef,
  useState,
} from "react";
import { createRoot } from "react-dom/client";

import {
  PROCESS_LIST_TOPIC,
  ProcessInfo,
  ProcessList,
  ProcessStatus,
  RemoteRequest,
  RequestId,
  SystemState,
  applyProcessLists,
  buildRequest,
  configName,
  parseProcessList,
  parseResponse,
  requestServiceName,
  statusLabel,
} from "./launchProtocol";
import { themeStyles } from "../common/theme";
import { styles } from "./launchStyles";

// The node publishes every 500 ms; this many missed seconds means the launch node is gone.
const STALE_AFTER_MS = 3000;
const MAX_LOG_ENTRIES = 8;

type LogEntry = {
  id: number;
  atMs: number;
  system: string;
  action: string;
  ok: boolean;
  message: string;
};

type SendRequest = (system: string, action: string, request: RemoteRequest) => void;

function badgeClass(status: number): string {
  switch (status) {
    case ProcessStatus.RUNNING:
      return "ok";
    case ProcessStatus.STARTING:
    case ProcessStatus.STOPPING:
    case ProcessStatus.RESTARTING:
    case ProcessStatus.TIMEOUT:
      return "warn";
    case ProcessStatus.DEAD:
    case ProcessStatus.INVALID:
      return "bad";
    default:
      return "idle";
  }
}

function errorMessage(error: unknown): string {
  return error instanceof Error ? error.message : String(error);
}

// A button for actions that take production processes down: the first click arms it, the second
// (within a few seconds) fires it.
function ConfirmButton(props: {
  label: string;
  confirmLabel: string;
  title: string;
  disabled: boolean;
  onConfirm: () => void;
}): ReactElement {
  const [armed, setArmed] = useState(false);

  useEffect(() => {
    if (!armed) {
      return undefined;
    }
    const timer = setTimeout(() => {
      setArmed(false);
    }, 3000);
    return () => {
      clearTimeout(timer);
    };
  }, [armed]);

  return (
    <button
      type="button"
      className={armed ? "bp-btn danger" : "bp-btn"}
      title={props.title}
      disabled={props.disabled}
      onClick={() => {
        if (armed) {
          setArmed(false);
          props.onConfirm();
        } else {
          setArmed(true);
        }
      }}
    >
      {armed ? props.confirmLabel : props.label}
    </button>
  );
}

function ProcessRow(props: {
  system: string;
  info: ProcessInfo;
  busy: boolean;
  send: SendRequest;
}): ReactElement {
  const { system, info, busy, send } = props;
  const act = (action: string, requestId: number): void => {
    send(system, `${action} ${info.processName}`, buildRequest(requestId, { processId: info.processId }));
  };
  return (
    <tr>
      <td className="num">{info.processId}</td>
      <td className="name">{info.processName}</td>
      <td>
        <span className={`bp-badge ${badgeClass(info.status)}`}>{statusLabel(info.status)}</span>
      </td>
      <td className="num">{info.restartCounter}</td>
      <td className="num">{info.pid > 0 ? info.pid : "–"}</td>
      <td className="num">{(info.cpuFraction * 100).toFixed(1)}</td>
      <td className="num">{(info.memoryBytes / 1e6).toFixed(1)}</td>
      <td>
        <div className="actions">
          <button type="button" className="bp-btn small" disabled={busy} onClick={() => { act("Start", RequestId.START); }}>
            Start
          </button>
          <button type="button" className="bp-btn small" disabled={busy} onClick={() => { act("Stop", RequestId.STOP); }}>
            Stop
          </button>
          <button type="button" className="bp-btn small" disabled={busy} onClick={() => { act("Restart", RequestId.RESTART); }}>
            Restart
          </button>
        </div>
      </td>
    </tr>
  );
}

function SystemCard(props: {
  state: SystemState;
  stale: boolean;
  busy: boolean;
  send: SendRequest;
}): ReactElement {
  const { state, stale, busy, send } = props;
  const [configPath, setConfigPath] = useState("");
  const running = state.processes.filter((p) => p.status === ProcessStatus.RUNNING).length;

  const submitConfig = (event: FormEvent): void => {
    event.preventDefault();
    const scenario = configPath.trim();
    if (scenario === "") {
      return;
    }
    send(state.systemName, `Load config ${scenario}`, buildRequest(RequestId.LOAD_CONFIG, { scenario }));
    setConfigPath("");
  };

  return (
    <section className="lc-system">
      <div className="lc-system-head">
        <span className="lc-system-name">{state.systemName}</span>
        <span className="lc-config" title={state.activeConfig}>
          {configName(state.activeConfig) || "(no config)"}
        </span>
        <span className="lc-spacer" />
        {stale && <span className="lc-stale">No updates</span>}
        <span className="lc-summary">
          {running}/{state.processes.length} running
        </span>
      </div>

      <div className="lc-controls">
        <button
          type="button"
          className="bp-btn"
          title="Start every process in the loaded launch configuration"
          disabled={busy}
          onClick={() => { send(state.systemName, "Launch", buildRequest(RequestId.LAUNCH)); }}
        >
          Launch
        </button>
        <ConfirmButton
          label="Shutdown"
          confirmLabel="Confirm shutdown?"
          title="Stop every process in the loaded launch configuration"
          disabled={busy}
          onConfirm={() => { send(state.systemName, "Shutdown", buildRequest(RequestId.SHUTDOWN)); }}
        />
        <ConfirmButton
          label="Hot reload"
          confirmLabel="Confirm reload?"
          title="Stop the launch, re-read the launch file and start it again"
          disabled={busy}
          onConfirm={() => { send(state.systemName, "Hot reload", buildRequest(RequestId.HOTLOAD)); }}
        />
        <form className="lc-config-form" onSubmit={submitConfig}>
          <input
            className="lc-input"
            value={configPath}
            placeholder="Launch file name or absolute path (launch must be shut down)"
            aria-label={`Launch config for ${state.systemName}`}
            onChange={(event) => { setConfigPath(event.target.value); }}
          />
          <button type="submit" className="bp-btn" disabled={busy || configPath.trim() === ""}>
            Load config
          </button>
        </form>
      </div>

      <table className="lc-table">
        <thead>
          <tr>
            <th className="num">ID</th>
            <th>Name</th>
            <th>Status</th>
            <th className="num">Restarts</th>
            <th className="num">PID</th>
            <th className="num">% CPU</th>
            <th className="num">Memory (MB)</th>
            <th>Actions</th>
          </tr>
        </thead>
        <tbody>
          {state.processes.map((info) => (
            <ProcessRow key={info.processId} system={state.systemName} info={info} busy={busy} send={send} />
          ))}
        </tbody>
      </table>
    </section>
  );
}

function LaunchControlPanel({ context }: { context: PanelExtensionContext }): ReactElement {
  const [renderDone, setRenderDone] = useState<(() => void) | undefined>();
  const [colorScheme, setColorScheme] = useState<"dark" | "light">("dark");
  const [topicAdvertised, setTopicAdvertised] = useState<boolean | undefined>();
  const [systems, setSystems] = useState<ReadonlyMap<string, SystemState>>(new Map());
  const [nowMs, setNowMs] = useState(() => Date.now());
  const [busySystems, setBusySystems] = useState<ReadonlySet<string>>(new Set());
  const [log, setLog] = useState<readonly LogEntry[]>([]);
  const nextLogId = useRef(0);

  useLayoutEffect(() => {
    context.onRender = (renderState: Immutable<RenderState>, done) => {
      setRenderDone(() => done);
      setColorScheme(renderState.colorScheme ?? "dark");

      if (renderState.topics) {
        setTopicAdvertised(renderState.topics.some((topic) => topic.name === PROCESS_LIST_TOPIC));
      }

      if (renderState.currentFrame) {
        const lists: ProcessList[] = [];
        for (const event of renderState.currentFrame) {
          if (event.topic !== PROCESS_LIST_TOPIC) {
            continue;
          }
          const list = parseProcessList(event.message);
          if (list) {
            lists.push(list);
          }
        }
        if (lists.length > 0) {
          const arrivedMs = Date.now();
          setSystems((previous) => applyProcessLists(previous, lists, arrivedMs));
        }
      }
    };

    context.watch("colorScheme");
    context.watch("topics");
    context.watch("currentFrame");
    context.subscribe([{ topic: PROCESS_LIST_TOPIC }]);

    return () => {
      context.onRender = undefined;
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

  const pushLog = useCallback((system: string, action: string, ok: boolean, message: string) => {
    const entry: LogEntry = { id: nextLogId.current++, atMs: Date.now(), system, action, ok, message };
    setLog((previous) => [entry, ...previous].slice(0, MAX_LOG_ENTRIES));
  }, []);

  const send = useCallback<SendRequest>(
    (system, action, request) => {
      if (context.callService == undefined) {
        pushLog(system, action, false, "This data source does not support service calls");
        return;
      }
      setBusySystems((previous) => new Set(previous).add(system));
      context
        .callService(requestServiceName(system), request)
        .then((raw) => {
          const response = parseResponse(raw);
          if (response) {
            pushLog(system, action, response.ok, response.message || `response code ${response.code}`);
          } else {
            pushLog(system, action, false, "Unexpected response from the launch node");
          }
        })
        .catch((error: unknown) => {
          pushLog(system, action, false, errorMessage(error));
        })
        .finally(() => {
          setBusySystems((previous) => {
            const next = new Set(previous);
            next.delete(system);
            return next;
          });
        });
    },
    [context, pushLog],
  );

  // Service calls need a live connection; staleness only means something there too, since a
  // paused or recorded source stops delivering lists without the node being gone.
  const live = context.callService != undefined;
  const sortedSystems = [...systems.values()].sort((a, b) => a.systemName.localeCompare(b.systemName));

  return (
    <div className="basin-panel" data-theme={colorScheme}>
      <style>{themeStyles}</style>
      <style>{styles}</style>

      {!live && (
        <div className="lc-readonly">
          Read-only: this data source can&apos;t call services, so process control is disabled.
        </div>
      )}

      {sortedSystems.length === 0 ? (
        <div className="bp-empty">
          {topicAdvertised === false ? (
            <>
              <code>{PROCESS_LIST_TOPIC}</code> isn&apos;t advertised. Check that the launch node is running and
              that Studio is connected to the foxglove_bridge.
            </>
          ) : (
            <>
              Waiting for a process list on <code>{PROCESS_LIST_TOPIC}</code>…
            </>
          )}
        </div>
      ) : (
        sortedSystems.map((state) => (
          <SystemCard
            key={state.systemName}
            state={state}
            stale={live && nowMs - state.lastSeenMs > STALE_AFTER_MS}
            busy={!live || busySystems.has(state.systemName)}
            send={send}
          />
        ))
      )}

      {log.length > 0 && (
        <ul className="lc-log">
          {log.map((entry) => (
            <li key={entry.id}>
              <span className="time">{new Date(entry.atMs).toLocaleTimeString()}</span>
              <span className={entry.ok ? "ok" : "bad"}>
                {entry.system}: {entry.action} — {entry.message}
              </span>
            </li>
          ))}
        </ul>
      )}
    </div>
  );
}

export function initLaunchControlPanel(context: PanelExtensionContext): () => void {
  const root = createRoot(context.panelElement);
  root.render(<LaunchControlPanel context={context} />);
  return () => {
    root.unmount();
  };
}
