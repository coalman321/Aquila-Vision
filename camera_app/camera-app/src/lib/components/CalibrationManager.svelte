<script lang="ts">
  import { onMount, onDestroy } from "svelte";
  import { CameraViewer, FileUploader } from "$framework";

  export let serverUrl: string = "http://localhost:8080";
  export let debugImageTopic: string = "/camera_cal/debug_image";
  export let pollIntervalMs: number = 1000;
  // Above this many samples calibrateCamera slows down sharply and the extra frames are mostly
  // redundant poses, so the user is told to cancel and start a fresh session.
  export let maxSamples: number = 50;

  // ── Status shape ──────────────────────────────────────────────────────────
  interface CalStatus {
    camera_name: string;
    session_id: number;
    cal_samples: number;
    reproj_err: number;
    min_x: number;
    max_x: number;
    min_y: number;
    max_y: number;
    max_skew: number;
    max_scale: number;
  }

  const emptyStatus: CalStatus = {
    camera_name: "",
    session_id: 0,
    cal_samples: 0,
    reproj_err: 0,
    min_x: 0,
    max_x: 0,
    min_y: 0,
    max_y: 0,
    max_skew: 0,
    max_scale: 0,
  };

  // ── State ─────────────────────────────────────────────────────────────────
  let status: CalStatus = { ...emptyStatus };
  let statusError: string | null = null;

  // Camera list
  let cameras: string[] = [];
  let camerasError: string | null = null;
  let camerasLoading = false;

  // Start‑form fields
  type BoardType = "chessboard" | "charuco";

  // Predefined ArUco dictionaries accepted by camera_cal_node (cv::aruco::PredefinedDictionaryType)
  const arucoDicts = [
    "DICT_4X4_50", "DICT_4X4_100", "DICT_4X4_250", "DICT_4X4_1000",
    "DICT_5X5_50", "DICT_5X5_100", "DICT_5X5_250", "DICT_5X5_1000",
    "DICT_6X6_50", "DICT_6X6_100", "DICT_6X6_250", "DICT_6X6_1000",
    "DICT_7X7_50", "DICT_7X7_100", "DICT_7X7_250", "DICT_7X7_1000",
    "DICT_ARUCO_ORIGINAL",
    "DICT_APRILTAG_16h5", "DICT_APRILTAG_25h9", "DICT_APRILTAG_36h10", "DICT_APRILTAG_36h11",
    "DICT_ARUCO_MIP_36h12",
  ];

  let startCameraName: string = "";
  let boardType: BoardType = "chessboard";
  // Chessboard: internal corners. ChArUco: squares.
  let boardRows: number = 7;
  let boardCols: number = 10;
  let edgeLengthMm: number = 22.86;
  // ChArUco only
  let markerLengthMm: number = 17.0;
  let arucoDict: string = "DICT_5X5_100";
  let legacyPattern = false;

  $: isCharuco = boardType === "charuco";
  $: dimsUnit = isCharuco ? "squares" : "internal corners";

  let busy = false;
  let toasts: Array<{ id: number; message: string; type: string }> = [];
  let _nextToastId = 1;

  let pollTimer: ReturnType<typeof setInterval> | null = null;

  // A calibration is running when session_id is non-zero (INACTIVE_SESSION = 0)
  $: isRunning = status.session_id !== 0;

  // A calibration result is available when there are samples and a non-zero reprojection error
  $: isCalibrated = status.cal_samples > 0 && status.reproj_err > 0;

  $: tooManySamples = isRunning && status.cal_samples > maxSamples;

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

  // ── API calls ─────────────────────────────────────────────────────────────
  async function fetchCameras() {
    camerasLoading = true;
    try {
      const res = await fetch(`${base()}/api/calibration/cameras`);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      cameras = await res.json();
      camerasError = null;
      // Reset selection if current value is no longer in list
      if (startCameraName && !cameras.includes(startCameraName)) {
        startCameraName = "";
      }
    } catch (e) {
      camerasError = e instanceof Error ? e.message : String(e);
    } finally {
      camerasLoading = false;
    }
  }

  async function fetchStatus() {
    try {
      const res = await fetch(`${base()}/api/calibration/status`);
      if (!res.ok) throw new Error(`HTTP ${res.status}`);
      status = await res.json();
      statusError = null;
    } catch (e) {
      statusError = e instanceof Error ? e.message : String(e);
    }
  }

  async function callAction(endpoint: string, body?: object) {
    busy = true;
    try {
      const res = await fetch(`${base()}${endpoint}`, {
        method: "POST",
        headers: body ? { "Content-Type": "application/json" } : undefined,
        body: body ? JSON.stringify(body) : undefined,
      });
      const j = await res.json();
      // The webserver answers 200 only for ERR_OK; anything else is an error status.
      if (!res.ok) {
        addToast(j.err_msg || j.error || `HTTP ${res.status}`, "error");
      } else {
        addToast(
          endpoint.includes("start")
            ? "Calibration started"
            : endpoint.includes("stop")
              ? "Calibration stopped"
              : "Calibration completed",
          "success"
        );
        await fetchStatus();
      }
    } catch (e) {
      addToast(e instanceof Error ? e.message : String(e), "error");
    } finally {
      busy = false;
    }
  }

  function handleStart() {
    if (!startCameraName.trim()) {
      addToast("Please select a camera before starting.", "error");
      return;
    }
    if (!Number.isInteger(boardRows) || !Number.isInteger(boardCols) || boardRows <= 0 || boardCols <= 0) {
      addToast("Board rows and columns must be positive whole numbers.", "error");
      return;
    }
    if (!edgeLengthMm || edgeLengthMm <= 0) {
      addToast("Edge length must be greater than 0.", "error");
      return;
    }
    if (isCharuco && (!markerLengthMm || markerLengthMm <= 0 || markerLengthMm >= edgeLengthMm)) {
      addToast("Marker length must be greater than 0 and less than the square edge length.", "error");
      return;
    }
    callAction("/api/calibration/start", {
      camera_name: startCameraName.trim(),
      board_type: boardType,
      board_rows: boardRows,
      board_cols: boardCols,
      edge_length_mm: edgeLengthMm,
      ...(isCharuco
        ? { marker_length_mm: markerLengthMm, aruco_dict: arucoDict, legacy_pattern: legacyPattern }
        : {}),
    });
  }

  function handleStop() {
    callAction("/api/calibration/stop");
  }

  function handleComplete() {
    callAction("/api/calibration/complete");
  }

  // ── FileUploader events ───────────────────────────────────────────────────
  function handleFileUploaded(event: CustomEvent) {
    addToast(`Uploaded: ${event.detail?.filename ?? "file"}`, "success");
  }

  function handleFileRemoved(event: CustomEvent) {
    addToast(`Removed: ${event.detail?.filename ?? "file"}`, "success");
  }

  // ── Lifecycle ─────────────────────────────────────────────────────────────
  onMount(() => {
    fetchCameras();
    fetchStatus();
    pollTimer = setInterval(fetchStatus, pollIntervalMs);
  });

  onDestroy(() => {
    if (pollTimer !== null) clearInterval(pollTimer);
  });

  // ── Progress‑bar helper ───────────────────────────────────────────────────
  /**
   * Clamp a 0‑1 coverage value and turn it into a percentage string.
   * Values outside [0,1] are clamped so the bar never overflows.
   */
  function pct(v: number): string {
    return `${Math.min(100, Math.max(0, v * 100)).toFixed(1)}%`;
  }

  function barClass(v: number): string {
    if (v >= 0.85) return "bar-fill good";
    if (v >= 0.5) return "bar-fill medium";
    return "bar-fill low";
  }

  /** For min_x / min_y which progress toward -1 (negate before display). */
  function pctMin(v: number): string {
    return pct(-v);
  }

  function barClassMin(v: number): string {
    return barClass(-v);
  }

  // Webrtc viewer port convention: main port + 1
  $: viewerUrl = (() => {
    try {
      const u = new URL(serverUrl);
      u.port = String(parseInt(u.port || "80") + 1);
      return u.toString().replace(/\/$/, "");
    } catch {
      return serverUrl;
    }
  })();
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

<div class="cal-manager">
  <!-- ── Title row ──────────────────────────────────────────────────────── -->
  <div class="header-row">
    <h2>Camera Calibration</h2>
    <span class="status-badge {isRunning ? 'running' : 'idle'}">
      {isRunning ? "● Running" : "○ Idle"}
    </span>
  </div>

  <!-- ── Main two-column layout ─────────────────────────────────────────── -->
  <div class="main-layout">

    <!-- LEFT: camera stream ──────────────────────────────────────────────── -->
    <div class="camera-panel">
      <h5>Debug Image</h5>
      <div class="camera-wrapper">
        <CameraViewer serverUrl={viewerUrl} defaultStream={debugImageTopic} streamFilter="/camera_cal" />
      </div>
    </div>

    <!-- RIGHT: controls + status ────────────────────────────────────────── -->
    <div class="right-panel">

      <!-- ── Start form ───────────────────────────────────────────────── -->
      <section class="card">
        <h5>Start Calibration</h5>
        <table class="form-table">
          <tbody>
            <tr>
              <td class="field">Board</td>
              <td class="value">
                <select class="board-select" bind:value={boardType} disabled={busy || isRunning}>
                  <option value="chessboard">Chessboard</option>
                  <option value="charuco">ChArUco</option>
                </select>
              </td>
            </tr>
            <tr>
              <td class="field">Size</td>
              <td class="value">
                <div class="edge-length-row">
                  <input
                    type="number"
                    bind:value={boardRows}
                    min="1"
                    step="1"
                    aria-label="Board rows"
                    disabled={busy || isRunning}
                  />
                  <span class="unit-label">×</span>
                  <input
                    type="number"
                    bind:value={boardCols}
                    min="1"
                    step="1"
                    aria-label="Board columns"
                    disabled={busy || isRunning}
                  />
                  <span class="unit-label">{dimsUnit}</span>
                </div>
              </td>
            </tr>
            <tr>
              <td class="field">Square Edge</td>
              <td class="value">
                <div class="edge-length-row">
                  <input
                    type="number"
                    bind:value={edgeLengthMm}
                    min="0.01"
                    step="0.01"
                    disabled={busy || isRunning}
                  />
                  <span class="unit-label">mm</span>
                </div>
              </td>
            </tr>
            {#if isCharuco}
              <tr>
                <td class="field">Marker Edge</td>
                <td class="value">
                  <div class="edge-length-row">
                    <input
                      type="number"
                      bind:value={markerLengthMm}
                      min="0.01"
                      step="0.01"
                      disabled={busy || isRunning}
                    />
                    <span class="unit-label">mm</span>
                  </div>
                </td>
              </tr>
              <tr>
                <td class="field">Dictionary</td>
                <td class="value">
                  <select class="board-select" bind:value={arucoDict} disabled={busy || isRunning}>
                    {#each arucoDicts as dict}
                      <option value={dict}>{dict}</option>
                    {/each}
                  </select>
                </td>
              </tr>
              <tr>
                <td class="field">Legacy Layout</td>
                <td class="value">
                  <label class="checkbox-label">
                    <input type="checkbox" bind:checked={legacyPattern} disabled={busy || isRunning} />
                    <span class="unit-label">Board generated by OpenCV &lt; 4.6</span>
                  </label>
                </td>
              </tr>
            {/if}
            <tr>
              <td class="field">Camera</td>
              <td class="value">
                <div class="camera-select-row">
                  {#if camerasError}
                    <span class="cameras-error">⚠ {camerasError}</span>
                  {:else if cameras.length === 0 && !camerasLoading}
                    <span class="cameras-empty">No cameras registered</span>
                  {:else}
                    <select bind:value={startCameraName} disabled={busy || camerasLoading}>
                      <option value="" disabled>— Select a camera —</option>
                      {#each cameras as cam}
                        <option value={cam}>{cam}</option>
                      {/each}
                    </select>
                  {/if}
                  <button
                    class="btn-refresh"
                    on:click={fetchCameras}
                    disabled={camerasLoading}
                    title="Refresh camera list"
                    aria-label="Refresh camera list"
                  >
                    {camerasLoading ? "…" : "↻"}
                  </button>
                </div>
              </td>
            </tr>
          </tbody>
        </table>

        <div class="action-row">
          <button class="btn-primary" on:click={handleStart} disabled={busy || isRunning}>
            Start
          </button>
          <button class="btn-secondary" on:click={handleStop} disabled={busy || !isRunning}>
            Stop
          </button>
          <button class="btn-success" on:click={handleComplete} disabled={busy || !isRunning}>
            Complete
          </button>
        </div>
      </section>

      <!-- ── Current session ──────────────────────────────────────────── -->
      {#if statusError}
        <div class="error-banner">⚠ Status unavailable: {statusError}</div>
      {:else}
        <section class="card">
          <h5>Session Info</h5>

          {#if isRunning}
            <dl class="info-grid">
              <dt>Camera</dt><dd>{status.camera_name}</dd>
              <dt>Samples</dt><dd>{status.cal_samples}</dd>
              <dt>Reprojection error</dt><dd>{status.reproj_err.toFixed(4)} px</dd>
              <dt>Status</dt><dd class="cal-status {isCalibrated ? 'ready' : 'collecting'}">{isCalibrated ? '✓ Ready' : 'Collecting…'}</dd>
            </dl>
            {#if tooManySamples}
              <div class="warning-banner" role="alert">
                <span>
                  ⚠ {status.cal_samples} samples collected (recommended maximum {maxSamples}). Calibrating
                  this many frames is slow and adds little accuracy; cancel and start over with fewer,
                  more varied poses.
                </span>
                <button class="btn-secondary" on:click={handleStop} disabled={busy}>Cancel</button>
              </div>
            {/if}
          {:else}
            <p class="muted">No calibration in progress.</p>
          {/if}
        </section>

        <!-- ── Coverage progress bars ──────────────────────────────────── -->
        <section class="card">
          <h5>Coverage</h5>
          <div class="bars">

            <div class="bar-row">
              <span class="bar-label">X min</span>
              <div class="bar-track">
                <div class={barClassMin(status.min_x)} style="width:{pctMin(status.min_x)}"></div>
              </div>
              <span class="bar-val">{pctMin(status.min_x)}</span>
            </div>

            <div class="bar-row">
              <span class="bar-label">X max</span>
              <div class="bar-track">
                <div class={barClass(status.max_x)} style="width:{pct(status.max_x)}"></div>
              </div>
              <span class="bar-val">{pct(status.max_x)}</span>
            </div>

            <div class="bar-row">
              <span class="bar-label">Y min</span>
              <div class="bar-track">
                <div class={barClassMin(status.min_y)} style="width:{pctMin(status.min_y)}"></div>
              </div>
              <span class="bar-val">{pctMin(status.min_y)}</span>
            </div>

            <div class="bar-row">
              <span class="bar-label">Y max</span>
              <div class="bar-track">
                <div class={barClass(status.max_y)} style="width:{pct(status.max_y)}"></div>
              </div>
              <span class="bar-val">{pct(status.max_y)}</span>
            </div>

            <div class="bar-row">
              <span class="bar-label">Skew</span>
              <div class="bar-track">
                <div class={barClass(status.max_skew)} style="width:{pct(status.max_skew)}"></div>
              </div>
              <span class="bar-val">{pct(status.max_skew)}</span>
            </div>

            <div class="bar-row">
              <span class="bar-label">Scale</span>
              <div class="bar-track">
                <div class={barClass(status.max_scale)} style="width:{pct(status.max_scale)}"></div>
              </div>
              <span class="bar-val">{pct(status.max_scale)}</span>
            </div>

          </div>

          <div class="legend">
            <span class="legend-dot good"></span><span>≥ 85 %</span>
            <span class="legend-dot medium"></span><span>≥ 50 %</span>
            <span class="legend-dot low"></span><span>&lt; 50 %</span>
          </div>
        </section>
      {/if}

    </div><!-- /right-panel -->
  </div><!-- /main-layout -->

  <!-- ── Calibration file uploads ────────────────────────────────────────── -->
  <section class="card upload-section">
    <h5>Calibration Files</h5>
    <p class="muted upload-hint">Upload and manage camera calibration files (JSON, YAML, etc.)</p>
    <FileUploader
      serverUrl={serverUrl}
      apiEndpoint={`${base()}/api/upload/calibrations`}
      hintText="Upload calibration files (JSON, YAML, etc.)"
      on:uploaded={handleFileUploaded}
      on:removed={handleFileRemoved}
    />
  </section>
</div>

<style>
  /* ── Layout ──────────────────────────────────────────────────────────── */
  .cal-manager {
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
    gap: 16px;
    align-items: flex-start;
    flex-wrap: wrap;
  }

  /* ── Camera panel ────────────────────────────────────────────────────── */
  .camera-panel {
    flex: 0 0 auto;
    background: #1a1a1a;
    border: 1px solid #2a2a2a;
    border-radius: 8px;
    padding: 12px;
    box-sizing: border-box;
  }

  .camera-wrapper {
    width: 640px;
    overflow: hidden;
  }

  /* ── Right panel ─────────────────────────────────────────────────────── */
  .right-panel {
    flex: 1 1 280px;
    display: flex;
    flex-direction: column;
    gap: 12px;
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

  .camera-select-row {
    display: flex;
    gap: 6px;
    align-items: center;
  }

  .camera-select-row select {
    flex: 1;
    padding: 6px 8px;
    border: 1px solid #333;
    border-radius: 4px;
    font-size: 0.9rem;
    background: #1a1a1a;
    color: #f9fafb;
    min-width: 0;
  }

  .board-select {
    padding: 6px 8px;
    border: 1px solid #333;
    border-radius: 4px;
    font-size: 0.9rem;
    background: #1a1a1a;
    color: #f9fafb;
    max-width: 100%;
  }

  .checkbox-label {
    display: flex;
    align-items: center;
    gap: 6px;
    cursor: pointer;
  }

  .edge-length-row {
    display: flex;
    align-items: center;
    gap: 6px;
  }

  .edge-length-row input[type="number"] {
    width: 90px;
    padding: 6px 8px;
    border: 1px solid #333;
    border-radius: 4px;
    font-size: 0.9rem;
    background: #1a1a1a;
    color: #f9fafb;
  }

  .unit-label {
    font-size: 0.875rem;
    color: #9ca3af;
  }

  .cameras-error {
    flex: 1;
    font-size: 0.85rem;
    color: #f87171;
  }

  .cameras-empty {
    flex: 1;
    font-size: 0.85rem;
    color: #9ca3af;
    font-style: italic;
  }

  .btn-refresh {
    padding: 6px 10px;
    border: 1px solid #333;
    border-radius: 4px;
    background: #1a1a1a;
    color: #d1d5db;
    cursor: pointer;
    font-size: 1rem;
    line-height: 1;
    flex-shrink: 0;
    transition: background 0.15s;
  }

  .btn-refresh:hover:not(:disabled) {
    background: #2a2a2a;
  }

  .btn-refresh:disabled {
    opacity: 0.5;
    cursor: not-allowed;
  }

  /* ── Action buttons ──────────────────────────────────────────────────── */
  .action-row {
    display: flex;
    gap: 8px;
    flex-wrap: wrap;
  }

  .btn-primary,
  .btn-secondary,
  .btn-success {
    padding: 8px 18px;
    border-radius: 6px;
    border: none;
    cursor: pointer;
    font-size: 0.9rem;
    font-weight: 600;
    transition: opacity 0.15s;
  }

  .btn-primary:disabled,
  .btn-secondary:disabled,
  .btn-success:disabled {
    opacity: 0.45;
    cursor: not-allowed;
  }

  .btn-primary {
    background: #dc2626;
    color: #fff;
  }

  .btn-primary:hover:not(:disabled) {
    background: #b91c1c;
  }

  .btn-secondary {
    background: #1a1a1a;
    border: 1px solid #333;
    color: #d1d5db;
  }

  .btn-secondary:hover:not(:disabled) {
    background: #2a2a2a;
  }

  .btn-success {
    background: #059669;
    color: #fff;
  }

  .btn-success:hover:not(:disabled) {
    background: #047857;
  }

  /* ── Info grid ───────────────────────────────────────────────────────── */
  .info-grid {
    display: grid;
    grid-template-columns: max-content 1fr;
    column-gap: 16px;
    row-gap: 4px;
    margin: 0;
    font-size: 0.9rem;
  }

  .info-grid dt {
    font-weight: 600;
    color: #9ca3af;
  }

  .info-grid dd {
    margin: 0;
    color: #f9fafb;
    word-break: break-all;
  }

  .cal-status.ready     { color: #34d399; font-weight: 600; }
  .cal-status.collecting { color: #fbbf24; font-weight: 600; }

  /* ── Progress bars ───────────────────────────────────────────────────── */
  .bars {
    display: flex;
    flex-direction: column;
    gap: 8px;
  }

  .bar-row {
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .bar-label {
    width: 54px;
    font-size: 0.8rem;
    font-weight: 600;
    color: #9ca3af;
    flex-shrink: 0;
  }

  .bar-track {
    flex: 1;
    height: 14px;
    background: #2a2a2a;
    border-radius: 999px;
    overflow: hidden;
  }

  .bar-fill {
    height: 100%;
    border-radius: 999px;
    transition: width 0.4s ease;
  }

  .bar-fill.good   { background: #10b981; }
  .bar-fill.medium { background: #f59e0b; }
  .bar-fill.low    { background: #ef4444; }

  .bar-val {
    width: 46px;
    font-size: 0.78rem;
    color: #e5e7eb;
    text-align: right;
    flex-shrink: 0;
  }

  /* ── Legend ──────────────────────────────────────────────────────────── */
  .legend {
    display: flex;
    align-items: center;
    gap: 6px;
    margin-top: 10px;
    font-size: 0.75rem;
    color: #9ca3af;
  }

  .legend-dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    display: inline-block;
    margin-left: 8px;
  }

  .legend-dot.good   { background: #10b981; }
  .legend-dot.medium { background: #f59e0b; }
  .legend-dot.low    { background: #ef4444; }

  .legend-dot:first-child { margin-left: 0; }

  /* ── Misc ────────────────────────────────────────────────────────────── */
  .muted {
    color: #9ca3af;
    font-style: italic;
    margin: 0;
  }

  .upload-section {
    width: 100%;
    box-sizing: border-box;
  }

  .upload-hint {
    margin: 0 0 12px 0;
    font-size: 0.875rem;
    color: #d1d5db;
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
    display: flex;
    align-items: center;
    gap: 12px;
    margin-top: 12px;
    background: #3b2a05;
    border: 1px solid #92400e;
    color: #fcd34d;
    border-radius: 6px;
    padding: 10px 14px;
    font-size: 0.875rem;
  }

  .warning-banner .btn-secondary {
    flex-shrink: 0;
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
