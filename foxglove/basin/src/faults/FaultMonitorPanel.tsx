import { Immutable, PanelExtensionContext, RenderState } from "@foxglove/extension";
import { ReactElement, useEffect, useLayoutEffect, useMemo, useState } from "react";
import { createRoot } from "react-dom/client";

import { themeStyles } from "../common/theme";
import { styles } from "./faultStyles";
import {
  EXPIRING_SOON_SEC,
  FAULT_REPORT_TOPIC,
  FaultEntry,
  FaultKey,
  applyFaultReports,
  entryKey,
  formatDateTime,
  formatDuration,
  isLive,
  parseFaultReport,
  setDismissed,
  undismissAll,
  visibleFaults,
} from "./faultProtocol";

function DetailRow(props: { label: string; children: string }): ReactElement {
  return (
    <>
      <dt>{props.label}</dt>
      <dd>{props.children}</dd>
    </>
  );
}

function FaultDetail(props: { entry: FaultEntry; nowSec: number; onClose: () => void }): ReactElement {
  const { entry, nowSec, onClose } = props;
  const remaining = entry.expirationTime - nowSec;
  return (
    <section className="fm-detail" aria-label="Fault detail">
      <div className="fm-detail-head">
        <span className="fm-detail-title">Fault detail</span>
        <span className="fm-spacer" />
        <button type="button" className="bp-btn small" onClick={onClose}>
          Close
        </button>
      </div>
      <dl>
        <DetailRow label="Source">{entry.source || "-"}</DetailRow>
        <DetailRow label="System">{entry.system}</DetailRow>
        <DetailRow label="Name">{entry.name}</DetailRow>
        <DetailRow label="Raised at">{formatDateTime(entry.raiseTime)}</DetailRow>
        <DetailRow label="Expires at">{formatDateTime(entry.expirationTime)}</DetailRow>
        <DetailRow label="Expires in">{formatDuration(remaining)}</DetailRow>
        <DetailRow label="Status">{remaining > 0 ? "Active" : "Inactive"}</DetailRow>
        <DetailRow label="Hidden">{entry.dismissed ? "yes" : "no"}</DetailRow>
      </dl>
      <div className="fm-desc-label">Description</div>
      <p className="fm-desc">{entry.desc || "(none)"}</p>
    </section>
  );
}

function FaultMonitorPanel({ context }: { context: PanelExtensionContext }): ReactElement {
  const [renderDone, setRenderDone] = useState<(() => void) | undefined>();
  const [colorScheme, setColorScheme] = useState<"dark" | "light">("dark");
  const [topicAdvertised, setTopicAdvertised] = useState<boolean | undefined>();
  const [faults, setFaults] = useState<ReadonlyMap<FaultKey, FaultEntry>>(new Map());
  // Faults expire against the data source's clock (Studio's playback time), not the wall clock, so
  // a paused or recorded source doesn't age its faults out from under you.
  const [nowSec, setNowSec] = useState(() => Date.now() / 1000);
  const [showDismissed, setShowDismissed] = useState(false);
  // The fault whose detail is showing: clicking a row selects it, Close deselects it.
  const [selectedKey, setSelectedKey] = useState<FaultKey | undefined>();

  useLayoutEffect(() => {
    context.onRender = (renderState: Immutable<RenderState>, done) => {
      setRenderDone(() => done);
      setColorScheme(renderState.colorScheme ?? "dark");

      let now: number | undefined;
      if (renderState.currentTime) {
        now = renderState.currentTime.sec + renderState.currentTime.nsec * 1e-9;
        setNowSec(now);
      }

      if (renderState.topics) {
        setTopicAdvertised(renderState.topics.some((topic) => topic.name === FAULT_REPORT_TOPIC));
      }

      // A seek jumps to a different point in the data, so what was live before says nothing about
      // what is live now; start over from the reports that follow.
      if (renderState.didSeek === true) {
        setFaults(new Map());
        setSelectedKey(undefined);
      }

      if (renderState.currentFrame) {
        const reports: FaultEntry[] = [];
        for (const event of renderState.currentFrame) {
          if (event.topic !== FAULT_REPORT_TOPIC) {
            continue;
          }
          const report = parseFaultReport(event.message);
          if (report) {
            reports.push(report);
          }
        }
        if (reports.length > 0) {
          const at = now ?? Date.now() / 1000;
          setFaults((previous) => applyFaultReports(previous, reports, at));
        }
      }
    };

    context.watch("colorScheme");
    context.watch("topics");
    context.watch("currentFrame");
    context.watch("currentTime");
    context.watch("didSeek");
    context.subscribe([{ topic: FAULT_REPORT_TOPIC }]);

    return () => {
      context.onRender = undefined;
    };
  }, [context]);

  // The host waits for this after every render before delivering the next one.
  useEffect(() => {
    renderDone?.();
  }, [renderDone]);

  const entries = useMemo(
    () => visibleFaults(faults, nowSec, showDismissed),
    [faults, nowSec, showDismissed],
  );
  const hiddenCount = useMemo(() => {
    if (showDismissed) {
      return 0;
    }
    let count = 0;
    for (const entry of faults.values()) {
      if (entry.dismissed && isLive(entry, nowSec)) {
        count += 1;
      }
    }
    return count;
  }, [faults, nowSec, showDismissed]);

  // Selection is tracked by fault key, not row index: rows reorder and drop out as faults come and
  // go, and an index would quietly point at a different fault than the one being looked at. The
  // detail comes from every fault seen, not the visible rows, so it stays put when its fault
  // flickers out of the list, e.g. a periodic health check that lapses for a frame before its next
  // report lands.
  const detailEntry = selectedKey != undefined ? faults.get(selectedKey) : undefined;

  const dismissSelected = (): void => {
    if (selectedKey != undefined) {
      setFaults((previous) => setDismissed(previous, [selectedKey], true));
    }
  };

  const dismissAll = (): void => {
    setFaults((previous) => setDismissed(previous, entries.map(entryKey), true));
  };

  return (
    <div className="basin-panel" data-theme={colorScheme}>
      <style>{themeStyles}</style>
      <style>{styles}</style>

      <div className="fm-toolbar">
        <div className="fm-toolbar-row">
          <span className="fm-summary" title={`Topic ${FAULT_REPORT_TOPIC}`}>
            <strong>{entries.length}</strong> live
            {hiddenCount > 0 && <span className="fm-muted"> · {hiddenCount} hidden</span>}
          </span>
        </div>
        <div className="fm-toolbar-row">
          <button type="button" className="bp-btn" title="Hide the selected fault" disabled={selectedKey == undefined} onClick={dismissSelected}>
            Hide
          </button>
          <button type="button" className="bp-btn" title="Hide every listed fault" disabled={entries.length === 0} onClick={dismissAll}>
            Hide all
          </button>
          <button type="button" className="bp-btn" title="Bring every hidden fault back" onClick={() => { setFaults(undismissAll); }}>
            Unhide all
          </button>
        </div>
        <div className="fm-toolbar-row">
          <button type="button" className={showDismissed ? "bp-btn on" : "bp-btn"} title="List hidden faults too" aria-pressed={showDismissed} onClick={() => { setShowDismissed((previous) => !previous); }}>
            Show Hidden
          </button>
        </div>
      </div>

      {entries.length === 0 ? (
        <div className="bp-empty">
          {topicAdvertised === false ? (
            <>
              <code>{FAULT_REPORT_TOPIC}</code> isn&apos;t advertised. Check that Studio is connected to the
              foxglove_bridge and that a node is publishing fault reports.
            </>
          ) : faults.size > 0 && hiddenCount > 0 ? (
            <>No live faults besides the {hiddenCount} hidden.</>
          ) : (
            <>No live faults.</>
          )}
        </div>
      ) : (
        <table className="fm-table">
          <thead>
            <tr>
              <th>System</th>
              <th>Name</th>
              <th className="num">Expires in</th>
            </tr>
          </thead>
          <tbody>
            {entries.map((entry) => {
              const remaining = entry.expirationTime - nowSec;
              const classes = [
                entryKey(entry) === selectedKey ? "selected" : "",
                entry.dismissed ? "hidden" : remaining < EXPIRING_SOON_SEC ? "expiring" : "",
              ]
                .filter((name) => name !== "")
                .join(" ");
              return (
                <tr
                  key={entryKey(entry)}
                  className={classes}
                  onClick={() => { setSelectedKey(entryKey(entry)); }}
                >
                  <td>{entry.system}</td>
                  <td>
                    {entry.name}
                    {entry.dismissed && <span className="fm-tag"> [hidden]</span>}
                  </td>
                  <td className="num">{formatDuration(remaining)}</td>
                </tr>
              );
            })}
          </tbody>
        </table>
      )}

      {detailEntry && (
        <FaultDetail
          entry={detailEntry}
          nowSec={nowSec}
          onClose={() => { setSelectedKey(undefined); }}
        />
      )}
    </div>
  );
}

export function initFaultMonitorPanel(context: PanelExtensionContext): () => void {
  const root = createRoot(context.panelElement);
  root.render(<FaultMonitorPanel context={context} />);
  return () => {
    root.unmount();
  };
}
