// Look shared by every panel: the root element's colors (following Studio's light/dark scheme through
// the data-theme attribute), buttons, status badges and the empty-state message. Each panel adds its
// own stylesheet for the rest, and puts the `basin-panel` class on its root element.
export const themeStyles = `
.basin-panel {
  --bg: #1b1b1f;
  --surface: #25252b;
  --border: #3a3a44;
  --text: #e6e6ea;
  --muted: #9a9aa6;
  --accent: #4f8cff;
  --ok: #3fb56b;
  --warn: #e0a030;
  --bad: #e5534b;
  box-sizing: border-box;
  height: 100%;
  overflow: auto;
  padding: 8px;
  background: var(--bg);
  color: var(--text);
  font: 12px/1.4 system-ui, sans-serif;
}
.basin-panel[data-theme="light"] {
  --bg: #ffffff;
  --surface: #f3f3f6;
  --border: #d0d0d8;
  --text: #1d1d22;
  --muted: #6a6a76;
  --accent: #1f5fd1;
  --ok: #1c8a4a;
  --warn: #a86a00;
  --bad: #c5352d;
}
.basin-panel *, .basin-panel *::before, .basin-panel *::after { box-sizing: border-box; }

.bp-empty { margin: 24px 8px; color: var(--muted); text-align: center; }
.bp-empty code { color: var(--text); }

.bp-btn { padding: 3px 10px; border: 1px solid var(--border); border-radius: 4px; background: var(--bg); color: var(--text); font: inherit; cursor: pointer; }
.bp-btn:hover:not(:disabled) { border-color: var(--accent); }
.bp-btn:disabled { opacity: 0.45; cursor: default; }
.bp-btn.danger { border-color: var(--bad); background: var(--bad); color: #fff; }
.bp-btn.small { padding: 1px 8px; }
.bp-btn.on { border-color: var(--accent); color: var(--accent); }

.bp-badge { padding: 0 8px; border-radius: 8px; border: 1px solid currentColor; font-weight: 600; }
.bp-badge.ok { color: var(--ok); }
.bp-badge.warn { color: var(--warn); }
.bp-badge.bad { color: var(--bad); }
.bp-badge.idle { color: var(--muted); }
`;
