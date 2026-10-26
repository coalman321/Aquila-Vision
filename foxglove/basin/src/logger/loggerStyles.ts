// Continuous Logger's own stylesheet, injected after the shared theme (../common/theme.ts), which
// supplies the colors, buttons and badges.
export const styles = `
.cl-readonly { margin: 0 0 8px; padding: 4px 8px; border: 1px solid var(--warn); border-radius: 4px; color: var(--warn); }

.cl-card { margin-bottom: 12px; border: 1px solid var(--border); border-radius: 6px; background: var(--surface); }
.cl-head { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; padding: 8px; }
.cl-title { font-weight: 600; font-size: 13px; }
.cl-muted { color: var(--muted); }
.cl-spacer { flex: 1 1 auto; }
.cl-section-title { padding: 8px 8px 0; font-weight: 600; font-size: 13px; }
.cl-hint { padding: 2px 8px 0; color: var(--muted); }

.cl-disk { padding: 8px; }
.cl-bar { height: 10px; border: 1px solid var(--border); border-radius: 5px; background: var(--bg); overflow: hidden; }
.cl-bar-fill { height: 100%; }
.cl-bar-fill.ok { background: var(--ok); }
.cl-bar-fill.warn { background: var(--warn); }
.cl-bar-fill.bad { background: var(--bad); }
.cl-disk-text { display: flex; flex-wrap: wrap; gap: 4px 16px; margin-top: 6px; }
.cl-disk-percent { font-weight: 600; font-size: 13px; }
.cl-path { margin-top: 2px; word-break: break-all; }

.cl-form { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; padding: 8px; }
.cl-input { flex: 1 1 120px; min-width: 0; padding: 3px 6px; border: 1px solid var(--border); border-radius: 4px; background: var(--bg); color: var(--text); font: inherit; }
.cl-input.wide { flex-basis: 220px; }
.cl-input:focus { outline: 1px solid var(--accent); }
.cl-check { display: flex; align-items: center; gap: 4px; }

.cl-table-wrap { overflow-x: auto; }
.cl-table { width: 100%; border-collapse: collapse; }
.cl-table th, .cl-table td { padding: 4px 8px; text-align: left; white-space: nowrap; vertical-align: top; }
.cl-table th { color: var(--muted); font-weight: 500; border-top: 1px solid var(--border); border-bottom: 1px solid var(--border); }
.cl-table tbody tr:nth-child(even) { background: rgba(127, 127, 127, 0.08); }
.cl-table .num { text-align: right; font-variant-numeric: tabular-nums; }
.cl-table .name { white-space: normal; word-break: break-all; }
.cl-table .tags { white-space: normal; }
/* A full date and time is long; let it break between the two rather than widen the table. */
.cl-table .when { white-space: normal; }
.cl-empty { padding: 8px; color: var(--muted); border-top: 1px solid var(--border); }

.cl-events { margin: 0; padding: 0; list-style: none; border-top: 1px solid var(--border); }
.cl-events li { display: flex; align-items: baseline; gap: 8px; padding: 2px 8px; }
.cl-events li:nth-child(even) { background: rgba(127, 127, 127, 0.08); }
.cl-events .time { flex: none; color: var(--muted); }
.cl-events .key { flex: none; font-weight: 600; }
.cl-events .yaml { flex: 1 1 auto; min-width: 0; overflow: hidden; text-overflow: ellipsis; white-space: nowrap; color: var(--muted); }

.cl-log { margin: 0; padding: 0; list-style: none; }
.cl-log li { padding: 2px 0; }
.cl-log .time { color: var(--muted); margin-right: 6px; }
.cl-log .ok { color: var(--ok); }
.cl-log .bad { color: var(--bad); }
`;
