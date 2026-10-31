<script lang="ts">
  import { onMount, onDestroy } from "svelte";

  export let serverUrl: string = "http://localhost:8080";
  export let pollIntervalMs: number = 1000;

  // ── Status shape (see logger_handler.hpp) ─────────────────────────────────
  interface DiskStatus {
    total_bytes: number;
    free_bytes: number;
    usage_percent: number;
    rotation_threshold_percent: number;
    log_bytes: number;
    snapshot_protected_bytes: number;
    snapshot_usage_percent: number;
    snapshot_usage_threshold_percent: number;
  }

  interface Snapshot {
    id: string;
    trigger: string;
    description: string;
    tags: string[];
    start_time: number;
    end_time: number;
    state: "running" | "pending" | "complete";
  }

  interface LoggerStatus {
    connected: boolean;
    log_path?: string;
    disk?: DiskStatus;
    snapshots?: Snapshot[];
    total_snapshot_count?: number;
  }

  // The look-back windows the webserver accepts
  const durations = [
    { seconds: 30, label: "30 s" },
    { seconds: 60, label: "60 s" },
    { seconds: 120, label: "2 min" },
    { seconds: 300, label: "5 min" },
  ];

  // Must match SNAPSHOT_NAME_REGEX in logger_handler.cpp
  const namePattern = /^[A-Za-z0-9_-]{1,64}$/;

  // ── State ─────────────────────────────────────────────────────────────────
  let status: LoggerStatus = { connected: false };
  let statusError: string | null = null;
  let loaded = false;
  let nowSec = Date.now() / 1000;

  // Create-form fields
  let snapshotName = "";
  let durationS = 60;
  let description = "";

  let busy = false;
  // ID of the snapshot being removed, if any
  let removing: string | null = null;
  let toasts: Array<{ id: number; message: string; type: string }> = [];
  let _nextToastId = 1;

  let pollTimer: ReturnType<typeof setInterval> | null = null;

  $: disk = status.disk;
  $: snapshots = status.snapshots ?? [];
  $: totalSnapshots = status.total_snapshot_count ?? 0;
  $: nameValid = namePattern.test(snapshotName);
  $: snapshotUsageHigh =
    disk !== undefined && disk.snapshot_usage_percent >= disk.snapshot_usage_threshold_percent;

  // Disk bar segments, in bytes. Other logs are those rotation may still delete; other data is
  // whatever else shares the filesystem.
  $: usedBytes = disk ? Math.max(0, disk.total_bytes - disk.free_bytes) : 0;
  $: protectedBytes = disk ? disk.snapshot_protected_bytes : 0;
  $: otherLogBytes = disk ? Math.max(0, disk.log_bytes - disk.snapshot_protected_bytes) : 0;
  $: otherDataBytes = disk ? Math.max(0, usedBytes - disk.log_bytes) : 0;

  // ── Helpers ───────────────────────────────────────────────────────────────
  function base() {
    return serverUrl.replace(/\/$/, "");
  }

  function addToast(message: string, type = "error") {
    const id = _nextToastId++;
    toasts = [...toasts, { id, message, type }];
    setTimeout(() => removeToast(id), 4500);
  }

  function removeToast(id: number) {
    toasts = toasts.filter((t) => t.id !== id);
  }

  function formatBytes(bytes: number): string {
    const units = ["B", "KB", "MB", "GB", "TB"];
    let value = bytes;
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) {
      value /= 1024;
      unit++;
    }
    return `${value.toFixed(unit === 0 ? 0 : 1)} ${units[unit]}`;
  }

  function share(bytes: number): string {
    if (!disk || disk.total_bytes === 0) return "0%";
    return `${Math.min(100, (bytes / disk.total_bytes) * 100).toFixed(2)}%`;
  }

  function formatTime(epochSec: number): string {
    return new Date(epochSec * 1000).toLocaleString();
  }

  function formatDuration(seconds: number): string {
    const s = Math.max(0, Math.round(seconds));
    const m = Math.floor(s / 60);
    return `${m}:${String(s % 60).padStart(2, "0")}`;
  }

  // ── API calls ─────────────────────────────────────────────────────────────
  async function fetchStatus() {
    nowSec = Date.now() / 1000;
    try {
      const res = await fetch(`${base()}/api/logger/status`);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      status = await res.json();
      statusError = null;
    } catch (e) {
      statusError = e instanceof Error ? e.message : String(e);
    } finally {
      loaded = true;
    }
  }

  async function handleCreate() {
    if (!nameValid) {
      addToast("Name must be 1-64 letters, digits, '_' or '-'.", "error");
      return;
    }
    busy = true;
    try {
      const res = await fetch(`${base()}/api/logger/snapshot`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          name: snapshotName,
          duration_s: durationS,
          ...(description.trim() ? { description: description.trim() } : {}),
        }),
      });
      const j = await res.json();
      if (!res.ok) {
        addToast(j.message || j.error || `HTTP ${res.status}`, "error");
      } else {
        addToast(j.message || "Snapshot captured", "success");
        snapshotName = "";
        description = "";
        await fetchStatus();
      }
    } catch (e) {
      addToast(e instanceof Error ? e.message : String(e), "error");
    } finally {
      busy = false;
    }
  }

  // A HEAD response has no body, so its failures are explained here
  const downloadErrors: Record<number, string> = {
    404: "The snapshot or its logs are no longer on disk.",
    409: "The snapshot is still recording.",
    429: "Too many downloads in progress; try again once one finishes.",
  };

  // Downloads in progress, by snapshot ID, as the server reports them
  interface DownloadState {
    phase: "checking" | "starting" | "preparing" | "sending" | "done";
    done_bytes: number;
    total_bytes: number;
  }
  let downloads: Record<string, DownloadState> = {};
  const DOWNLOAD_POLL_MS = 500;
  // How long the server may take to start a download before the page stops watching
  const DOWNLOAD_START_TIMEOUT_MS = 30000;

  function setDownload(id: string, state: DownloadState | null) {
    const next = { ...downloads };
    if (state) next[id] = state;
    else delete next[id];
    downloads = next;
  }

  function downloadLabel(state: DownloadState): string {
    const percent = state.total_bytes > 0 ? Math.floor((state.done_bytes / state.total_bytes) * 100) : 0;
    switch (state.phase) {
      case "checking":
        return "Checking…";
      case "starting":
        return "Starting…";
      case "preparing":
        return `Preparing ${percent}%`;
      case "sending":
        return `Downloading ${percent}%`;
      case "done":
        return "Downloaded";
    }
  }

  function downloadTitle(state: DownloadState): string {
    switch (state.phase) {
      case "preparing":
        return `Checksumming the logs before sending: ${formatBytes(state.done_bytes)} of ${formatBytes(state.total_bytes)}`;
      case "sending":
        return `Sent ${formatBytes(state.done_bytes)} of ${formatBytes(state.total_bytes)}`;
      default:
        return "";
    }
  }

  // A token for the server to file the download's progress under; crypto.randomUUID needs HTTPS
  function progressToken(): string {
    const bytes = crypto.getRandomValues(new Uint8Array(16));
    return Array.from(bytes, (b) => b.toString(16).padStart(2, "0")).join("");
  }

  function delay(ms: number): Promise<void> {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  // Stops watching downloads once the page is left; the browser's downloads carry on
  let destroyed = false;

  async function handleDownload(snap: Snapshot) {
    const url = `${base()}/api/logger/snapshot/download?id=${encodeURIComponent(snap.id)}`;
    setDownload(snap.id, { phase: "checking", done_bytes: 0, total_bytes: 0 });
    // Check first, so a failure shows here rather than as a broken download in the browser
    try {
      const res = await fetch(url, { method: "HEAD" });
      if (!res.ok) {
        addToast(downloadErrors[res.status] ?? `Download failed: HTTP ${res.status}`, "error");
        setDownload(snap.id, null);
        return;
      }
    } catch (e) {
      addToast(e instanceof Error ? e.message : String(e), "error");
      setDownload(snap.id, null);
      return;
    }

    // A plain link, so the browser streams it straight to disk with its own progress. The server can't
    // send the first byte until it has checksummed every log, so its progress is shown here until then.
    const token = progressToken();
    const link = document.createElement("a");
    link.href = `${url}&progress=${token}`;
    link.download = "";
    link.click();
    setDownload(snap.id, { phase: "starting", done_bytes: 0, total_bytes: 0 });
    await watchDownload(snap.id, token);
  }

  async function watchDownload(id: string, token: string) {
    const progressUrl = `${base()}/api/logger/snapshot/download/progress?token=${token}`;
    const started = Date.now();
    while (!destroyed) {
      await delay(DOWNLOAD_POLL_MS);
      let j;
      try {
        const res = await fetch(progressUrl);
        if (res.status === 404) {
          // The request hasn't reached the server yet
          if (Date.now() - started < DOWNLOAD_START_TIMEOUT_MS) continue;
          addToast(`Download of ${id} did not start.`, "error");
          break;
        }
        if (!res.ok) throw new Error(`HTTP ${res.status}`);
        j = await res.json();
      } catch {
        // Lost track of it, but the browser's download carries on regardless
        break;
      }
      if (j.phase === "failed") {
        addToast(`Download of ${id} failed: ${j.error ?? "unknown error"}`, "error");
        break;
      }
      if (j.phase === "stopped") {
        addToast(`Download of ${id} stopped before it finished.`, "error");
        break;
      }
      setDownload(id, { phase: j.phase, done_bytes: j.done_bytes, total_bytes: j.total_bytes });
      if (j.phase === "done") {
        await delay(3000);
        break;
      }
    }
    setDownload(id, null);
  }

  async function handleRemove(snap: Snapshot) {
    if (
      !confirm(
        `Remove snapshot "${snap.id}"?\n\n` +
          "Its logs stay on disk, but are no longer protected: rotation will delete them once it needs the space. " +
          "This cannot be undone."
      )
    ) {
      return;
    }
    removing = snap.id;
    try {
      const res = await fetch(`${base()}/api/logger/snapshot?id=${encodeURIComponent(snap.id)}`, {
        method: "DELETE",
      });
      const j = await res.json().catch(() => ({}));
      if (!res.ok) {
        addToast(j.message || j.error || `HTTP ${res.status}`, "error");
      } else {
        addToast(j.message || "Snapshot removed", "success");
        await fetchStatus();
      }
    } catch (e) {
      addToast(e instanceof Error ? e.message : String(e), "error");
    } finally {
      removing = null;
    }
  }

  // ── Lifecycle ─────────────────────────────────────────────────────────────
  onMount(() => {
    fetchStatus();
    pollTimer = setInterval(fetchStatus, pollIntervalMs);
  });

  onDestroy(() => {
    if (pollTimer !== null) clearInterval(pollTimer);
    destroyed = true;
  });
</script>

<!-- ── Toast container ─────────────────────────────────────────────────── -->
<div class="toast-container" aria-live="polite">
  {#each toasts as toast (toast.id)}
    <div class="toast {toast.type}">
      <span>{toast.message}</span>
      <button on:click={() => removeToast(toast.id)} aria-label="Dismiss">×</button>
    </div>
  {/each}
</div>

<div class="logger-manager">
  <!-- ── Title row ──────────────────────────────────────────────────────── -->
  <div class="header-row">
    <h2>Logger</h2>
    <span class="status-badge {status.connected ? 'running' : 'idle'}">
      {status.connected ? "● Recording" : "○ Not running"}
    </span>
  </div>

  {#if statusError}
    <div class="error-banner">Could not reach the webserver: {statusError}</div>
  {:else if loaded && !status.connected}
    <div class="error-banner">
      No status from the continuous logger. Check that continuous_logger_node is running on the Status tab.
    </div>
  {/if}

  <div class="main-layout">
    <!-- ── Disk usage ───────────────────────────────────────────────────── -->
    <section class="card disk-card">
      <h5>Disk Usage</h5>
      {#if disk}
        <div class="disk-bar" role="img" aria-label="Disk usage {disk.usage_percent.toFixed(1)}%">
          <div class="seg protected" style="width: {share(protectedBytes)}"></div>
          <div class="seg logs" style="width: {share(otherLogBytes)}"></div>
          <div class="seg other" style="width: {share(otherDataBytes)}"></div>
          <div
            class="marker rotation"
            style="left: {disk.rotation_threshold_percent}%"
            title="Rotation threshold {disk.rotation_threshold_percent}%"
          ></div>
          <div
            class="marker fault"
            style="left: {disk.snapshot_usage_threshold_percent}%"
            title="Snapshot fault threshold {disk.snapshot_usage_threshold_percent}%"
          ></div>
        </div>

        <dl class="info-grid">
          <dt><span class="legend-dot protected"></span>Snapshots</dt>
          <dd>{formatBytes(protectedBytes)} ({disk.snapshot_usage_percent.toFixed(1)}%)</dd>
          <dt><span class="legend-dot logs"></span>Logs</dt>
          <dd>{formatBytes(otherLogBytes)}</dd>
          <dt><span class="legend-dot other"></span>Other data</dt>
          <dd>{formatBytes(otherDataBytes)}</dd>
          <dt><span class="legend-dot free"></span>Free</dt>
          <dd>{formatBytes(disk.free_bytes)} of {formatBytes(disk.total_bytes)}</dd>
          <dt><span class="legend-line rotation"></span>Rotation at</dt>
          <dd>{disk.rotation_threshold_percent}% (now {disk.usage_percent.toFixed(1)}%)</dd>
          <dt><span class="legend-line fault"></span>Snapshot fault at</dt>
          <dd>{disk.snapshot_usage_threshold_percent}%</dd>
          <dt>Log path</dt>
          <dd class="path">{status.log_path}</dd>
        </dl>

        {#if snapshotUsageHigh}
          <div class="warning-banner">
            Snapshots fill {disk.snapshot_usage_percent.toFixed(1)}% of the disk, at or above the
            {disk.snapshot_usage_threshold_percent}% limit. Rotation can't delete them: copy them off and remove
            them to free space.
          </div>
        {/if}
      {:else}
        <p class="muted">No disk information yet.</p>
      {/if}
    </section>

    <!-- ── Create snapshot ──────────────────────────────────────────────── -->
    <section class="card create-card">
      <h5>Save Snapshot</h5>
      <table class="form-table">
        <tbody>
          <tr>
            <td class="field">Name</td>
            <td class="value">
              <input
                type="text"
                bind:value={snapshotName}
                placeholder="e.g. bad_pose_q12"
                maxlength="64"
                class:invalid={snapshotName !== "" && !nameValid}
                disabled={busy}
              />
            </td>
          </tr>
          <tr>
            <td class="field">Save last</td>
            <td class="value">
              <select bind:value={durationS} disabled={busy}>
                {#each durations as d (d.seconds)}
                  <option value={d.seconds}>{d.label}</option>
                {/each}
              </select>
            </td>
          </tr>
          <tr>
            <td class="field">Description</td>
            <td class="value">
              <input type="text" bind:value={description} placeholder="Optional" disabled={busy} />
            </td>
          </tr>
        </tbody>
      </table>
      {#if snapshotName !== "" && !nameValid}
        <p class="hint error">Letters, digits, '_' and '-' only, up to 64 characters.</p>
      {/if}
      <div class="action-row">
        <button class="btn-primary" on:click={handleCreate} disabled={busy || !nameValid || !status.connected}>
          {busy ? "Saving…" : "Save Snapshot"}
        </button>
      </div>
    </section>
  </div>

  <!-- ── Snapshots ────────────────────────────────────────────────────────── -->
  <section class="card">
    <h5>Snapshots</h5>
    {#if snapshots.length === 0}
      <p class="muted">No snapshots on disk.</p>
    {:else}
      <div class="table-scroll">
        <table class="snapshot-table">
          <thead>
            <tr>
              <th>ID</th>
              <th>State</th>
              <th>Trigger</th>
              <th>Start</th>
              <th>Length</th>
              <th>Tags</th>
              <th>Description</th>
              <th></th>
            </tr>
          </thead>
          <tbody>
            {#each snapshots as snap (snap.id)}
              <tr>
                <td class="mono">{snap.id}</td>
                <td><span class="state {snap.state}">{snap.state}</span></td>
                <td>{snap.trigger}</td>
                <td>{formatTime(snap.start_time)}</td>
                <td>
                  {#if snap.end_time < 0}
                    Recording… {formatDuration(nowSec - snap.start_time)}
                  {:else}
                    {formatDuration(snap.end_time - snap.start_time)}
                  {/if}
                </td>
                <td>
                  {#each snap.tags as tag (tag)}
                    <span class="tag">{tag}</span>
                  {/each}
                </td>
                <td>{snap.description}</td>
                <td>
                  {#if snap.state === "complete"}
                    {@const download = downloads[snap.id]}
                    <button
                      class="btn-download"
                      class:active={download !== undefined}
                      style:--progress={download && download.total_bytes > 0
                        ? `${(download.done_bytes / download.total_bytes) * 100}%`
                        : "0%"}
                      on:click={() => handleDownload(snap)}
                      disabled={download !== undefined}
                      title={download
                        ? downloadTitle(download)
                        : "Download the snapshot's log files as a zip"}
                    >
                      {download ? downloadLabel(download) : "Download"}
                    </button>
                    <button
                      class="btn-remove"
                      on:click={() => handleRemove(snap)}
                      disabled={removing !== null || !status.connected}
                      title="Remove the snapshot's protection; its logs are kept until rotation needs the space"
                    >
                      {removing === snap.id ? "Removing…" : "Remove"}
                    </button>
                  {/if}
                </td>
              </tr>
            {/each}
          </tbody>
        </table>
      </div>
      {#if totalSnapshots > snapshots.length}
        <p class="hint">Showing the {snapshots.length} most recent of {totalSnapshots} snapshots.</p>
      {/if}
    {/if}
  </section>
</div>

<style>
  .logger-manager {
    display: flex;
    flex-direction: column;
    gap: 12px;
    width: 100%;
    box-sizing: border-box;
    color: #f9fafb;
  }

  .header-row {
    display: flex;
    align-items: center;
    gap: 12px;
  }

  h2 {
    margin: 0;
    font-size: 1.5rem;
    font-weight: 600;
    color: #f9fafb;
  }

  h5 {
    margin: 0 0 10px 0;
    font-size: 1rem;
    font-weight: 600;
    color: #e5e7eb;
  }

  .main-layout {
    display: flex;
    gap: 12px;
    align-items: stretch;
    flex-wrap: wrap;
  }

  .disk-card {
    flex: 2 1 360px;
    min-width: 0;
  }

  .create-card {
    flex: 1 1 280px;
    min-width: 0;
  }

  /* ── Card ────────────────────────────────────────────────────────────── */
  .card {
    background: #222222;
    border: 1px solid #2a2a2a;
    border-radius: 8px;
    padding: 14px 16px;
  }

  /* ── Status badge ────────────────────────────────────────────────────── */
  .status-badge {
    font-size: 0.8rem;
    font-weight: 600;
    padding: 3px 10px;
    border-radius: 999px;
  }

  .status-badge.running {
    background: #064e3b;
    color: #6ee7b7;
  }

  .status-badge.idle {
    background: #2a2a2a;
    color: #9ca3af;
  }

  /* ── Disk bar ────────────────────────────────────────────────────────── */
  .disk-bar {
    position: relative;
    display: flex;
    height: 22px;
    background: #374151;
    border-radius: 4px;
    overflow: hidden;
    margin-bottom: 12px;
  }

  .seg {
    height: 100%;
  }

  .seg.protected,
  .legend-dot.protected { background: #dc2626; }
  .seg.logs,
  .legend-dot.logs      { background: #f59e0b; }
  .seg.other,
  .legend-dot.other     { background: #6b7280; }
  .legend-dot.free      { background: #374151; }

  .marker {
    position: absolute;
    top: 0;
    bottom: 0;
    width: 2px;
    transform: translateX(-1px);
  }

  .marker.rotation,
  .legend-line.rotation { background: #f9fafb; }
  .marker.fault,
  .legend-line.fault    { background: #60a5fa; }

  .legend-dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    display: inline-block;
    margin-right: 6px;
  }

  .legend-line {
    width: 3px;
    height: 12px;
    display: inline-block;
    margin: 0 9px 0 4px;
    vertical-align: middle;
  }

  /* ── Info grid ───────────────────────────────────────────────────────── */
  .info-grid {
    display: grid;
    grid-template-columns: max-content 1fr;
    column-gap: 16px;
    row-gap: 4px;
    margin: 0;
    font-size: 0.875rem;
  }

  .info-grid dt {
    color: #9ca3af;
    display: flex;
    align-items: center;
  }

  .info-grid dd {
    margin: 0;
    color: #f9fafb;
    font-variant-numeric: tabular-nums;
  }

  .info-grid dd.path {
    font-family: monospace;
    overflow-wrap: anywhere;
  }

  /* ── Form table ──────────────────────────────────────────────────────── */
  .form-table {
    width: 100%;
    border-collapse: collapse;
    margin-bottom: 12px;
  }

  .form-table td.field {
    font-weight: 600;
    padding: 6px 10px 6px 0;
    white-space: nowrap;
    color: #e5e7eb;
    width: 110px;
    vertical-align: middle;
  }

  .form-table td.value {
    padding: 4px 0;
  }

  .form-table input,
  .form-table select {
    width: 100%;
    box-sizing: border-box;
    padding: 6px 8px;
    background: #1a1a1a;
    border: 1px solid #333;
    border-radius: 6px;
    color: #f9fafb;
    font-size: 0.9rem;
  }

  .form-table input.invalid {
    border-color: #ef4444;
  }

  .action-row {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
  }

  .btn-primary {
    padding: 8px 18px;
    border-radius: 6px;
    border: none;
    cursor: pointer;
    font-size: 0.9rem;
    font-weight: 600;
    transition: opacity 0.15s;
    background: #dc2626;
    color: #fff;
  }

  .btn-primary:disabled {
    opacity: 0.45;
    cursor: not-allowed;
  }

  .btn-primary:hover:not(:disabled) {
    background: #b91c1c;
  }

  .btn-download {
    padding: 3px 10px;
    margin-right: 6px;
    border-radius: 6px;
    background: #1a1a1a;
    border: 1px solid #333;
    color: #d1d5db;
    font-size: 0.8rem;
    cursor: pointer;
    white-space: nowrap;
  }

  .btn-download:hover:not(:disabled) {
    background: #2a2a2a;
  }

  /* While downloading, the button fills from the left as progress is made */
  .btn-download.active {
    min-width: 9.5em;
    cursor: default;
    color: #93c5fd;
    border-color: #1e3a5f;
    background: linear-gradient(to right, #1e3a5f var(--progress), #1a1a1a var(--progress));
    font-variant-numeric: tabular-nums;
  }

  .btn-remove {
    padding: 3px 10px;
    border-radius: 6px;
    background: #1a1a1a;
    border: 1px solid #7f1d1d;
    color: #fca5a5;
    font-size: 0.8rem;
    cursor: pointer;
    white-space: nowrap;
  }

  .btn-remove:hover:not(:disabled) {
    background: #3b0a0a;
  }

  .btn-remove:disabled {
    opacity: 0.45;
    cursor: not-allowed;
  }

  /* ── Snapshot table ──────────────────────────────────────────────────── */
  .table-scroll {
    overflow-x: auto;
  }

  .snapshot-table {
    width: 100%;
    border-collapse: collapse;
    font-size: 0.85rem;
  }

  .snapshot-table th {
    text-align: left;
    color: #9ca3af;
    font-weight: 600;
    padding: 6px 10px;
    border-bottom: 1px solid #333;
    white-space: nowrap;
  }

  .snapshot-table td {
    padding: 6px 10px;
    border-bottom: 1px solid #2a2a2a;
    vertical-align: top;
  }

  .snapshot-table td.mono {
    font-family: monospace;
    white-space: nowrap;
  }

  .state {
    font-size: 0.75rem;
    font-weight: 600;
    padding: 2px 8px;
    border-radius: 999px;
    text-transform: capitalize;
  }

  .state.running,
  .state.pending  { background: #064e3b; color: #6ee7b7; }
  .state.complete { background: #2a2a2a; color: #d1d5db; }

  .tag {
    display: inline-block;
    font-size: 0.75rem;
    padding: 1px 6px;
    margin: 0 4px 2px 0;
    border-radius: 4px;
    background: #3b0a0a;
    color: #f87171;
  }

  /* ── Misc ────────────────────────────────────────────────────────────── */
  .muted {
    color: #9ca3af;
    font-style: italic;
    margin: 0;
  }

  .hint {
    margin: 8px 0 0 0;
    font-size: 0.8rem;
    color: #9ca3af;
  }

  .hint.error {
    margin: -4px 0 10px 0;
    color: #fca5a5;
  }

  .error-banner {
    background: #3b0a0a;
    border: 1px solid #7f1d1d;
    color: #fca5a5;
    border-radius: 6px;
    padding: 10px 14px;
    font-size: 0.875rem;
  }

  .warning-banner {
    margin-top: 12px;
    background: #3b2a05;
    border: 1px solid #92400e;
    color: #fcd34d;
    border-radius: 6px;
    padding: 10px 14px;
    font-size: 0.875rem;
  }

  /* ── Toasts ──────────────────────────────────────────────────────────── */
  .toast-container {
    position: fixed;
    top: 16px;
    right: 16px;
    z-index: 1000;
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .toast {
    display: flex;
    align-items: center;
    gap: 10px;
    padding: 9px 14px;
    border-radius: 6px;
    font-size: 0.875rem;
    box-shadow: 0 2px 6px rgba(0,0,0,0.4);
  }

  .toast.error   { background: #dc2626; color: #fff; }
  .toast.success { background: #059669; color: #fff; }

  .toast button {
    background: transparent;
    border: none;
    color: inherit;
    font-size: 1.1rem;
    cursor: pointer;
    padding: 0 2px;
    line-height: 1;
  }
</style>
