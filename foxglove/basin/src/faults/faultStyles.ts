// Fault Monitor's own stylesheet, injected after the shared theme (../common/theme.ts), which
// supplies the colors, buttons and empty-state message.
export const styles = `
.fm-spacer { flex: 1 1 auto; }
.fm-muted { color: var(--muted); }

.fm-toolbar { display: flex; flex-direction: column; gap: 6px; margin-bottom: 8px; }
.fm-toolbar-row { display: flex; flex-wrap: wrap; align-items: center; gap: 6px; }
.fm-summary { font-size: 13px; }

.fm-table { width: 100%; border-collapse: collapse; table-layout: fixed; }
.fm-table th, .fm-table td { padding: 4px 8px; text-align: left; }
.fm-table th { position: sticky; top: -8px; background: var(--bg); color: var(--muted); font-weight: 500; border-bottom: 1px solid var(--border); }
.fm-table td { overflow-wrap: break-word; word-break: break-word; }
.fm-table tbody tr { cursor: pointer; }
.fm-table tbody tr:nth-child(even) { background: rgba(127, 127, 127, 0.08); }
.fm-table tbody tr.expiring { color: var(--warn); }
.fm-table tbody tr.dismissed { color: var(--muted); }
.fm-table tbody tr.selected { background: var(--accent); color: #fff; }
.fm-table th:first-child, .fm-table td:first-child { width: 32%; }
/* Fixed px width, not a %: this column's content ("Expires in" / "12.3s") never needs to wrap,
   so it gets just enough room to render on one line and nothing is reserved beyond that. */
.fm-table .num { width: 64px; white-space: nowrap; text-align: right; font-variant-numeric: tabular-nums; }
.fm-tag { font-style: italic; }

.fm-detail { margin-top: 12px; padding: 8px; border: 1px solid var(--border); border-radius: 6px; background: var(--surface); }
.fm-detail-head { display: flex; align-items: center; gap: 8px; margin-bottom: 6px; }
.fm-detail-title { font-weight: 600; font-size: 13px; }
.fm-detail dl { display: grid; grid-template-columns: max-content 1fr; gap: 2px 12px; margin: 0; }
.fm-detail dt { color: var(--muted); }
.fm-detail dd { margin: 0; word-break: break-all; }
.fm-desc-label { margin-top: 8px; color: var(--muted); }
.fm-desc { margin: 2px 0 0; white-space: pre-wrap; word-break: break-word; }
`;
