// Launch Control's own stylesheet, injected after the shared theme (../common/theme.ts), which
// supplies the colors, buttons and badges.
export const styles = `
.lc-readonly { margin: 0 0 8px; padding: 4px 8px; border: 1px solid var(--warn); border-radius: 4px; color: var(--warn); }

.lc-system { margin-bottom: 12px; border: 1px solid var(--border); border-radius: 6px; background: var(--surface); }
.lc-system-head { display: flex; flex-wrap: wrap; align-items: center; gap: 8px; padding: 8px; border-bottom: 1px solid var(--border); }
.lc-system-name { font-weight: 600; font-size: 13px; }
.lc-config { color: var(--muted); }
.lc-summary { color: var(--muted); }
.lc-spacer { flex: 1 1 auto; }
.lc-stale { padding: 0 6px; border-radius: 8px; background: var(--bad); color: #fff; font-weight: 600; }

.lc-controls { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; padding: 8px; border-bottom: 1px solid var(--border); }
.lc-config-form { display: flex; flex: 1 1 200px; gap: 6px; }
.lc-input { flex: 1 1 auto; min-width: 0; padding: 3px 6px; border: 1px solid var(--border); border-radius: 4px; background: var(--bg); color: var(--text); font: inherit; }
.lc-input:focus { outline: 1px solid var(--accent); }

.lc-table { width: 100%; border-collapse: collapse; }
.lc-table th, .lc-table td { padding: 4px 8px; text-align: left; white-space: nowrap; }
.lc-table th { color: var(--muted); font-weight: 500; border-bottom: 1px solid var(--border); }
.lc-table tbody tr:nth-child(even) { background: rgba(127, 127, 127, 0.08); }
.lc-table .num { text-align: right; font-variant-numeric: tabular-nums; }
.lc-table .name { white-space: normal; word-break: break-all; }
.lc-table .actions { display: flex; gap: 4px; }

.lc-log { margin: 0; padding: 0; list-style: none; }
.lc-log li { padding: 2px 0; }
.lc-log .time { color: var(--muted); margin-right: 6px; }
.lc-log .ok { color: var(--ok); }
.lc-log .bad { color: var(--bad); }
`;
