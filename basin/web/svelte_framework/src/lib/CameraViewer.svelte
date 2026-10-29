<script lang="ts">
	import { untrack } from "svelte";
	import CameraViewerBase from "./CameraViewerBase.svelte";
	import type { StreamFilter, StreamInfo, StreamStats } from "./CameraViewerBase.svelte";

	// Props
	let {
		serverUrl = "http://localhost:8080",
		defaultStream = "default",
		streamFilter,
		width = 640,
		height = 480,
		className = "",
	}: {
		serverUrl?: string;
		defaultStream?: string;
		streamFilter?: StreamFilter;
		width?: number;
		height?: number;
		className?: string;
	} = $props();

	// Base component reference
	let base = $state<ReturnType<typeof CameraViewerBase> | null>(null);

	// State bound from / mirrored to the base component
	let selectedStreamId = $state(untrack(() => defaultStream));
	let isStreaming = $state(false);
	let error = $state<string | null>(null);
	let availableStreams = $state<StreamInfo[]>([]);
	let stats = $state<StreamStats>({
		bytesReceived: 0,
		packetsReceived: 0,
		packetsLost: 0,
		framesDecoded: 0,
		fps: 0,
		bitrate: 0,
	});

	// UI-only state
	let showSettings = $state(false);
	let showStats = $state(false);

	// Settings panel local values
	let targetBitrate = $state(2000000);
	let maxFramerate = $state(30);
	let resolutionScale = $state(1.0);
	let encoderSpeed = $state(4);

	// ── Handlers ──────────────────────────────────────────────────────────────

	async function handleStreamChange() {
		if (selectedStreamId) {
			await base?.connect(selectedStreamId);
		}
	}

	async function refreshStreams() {
		await base?.queryAvailableStreams();
	}

	async function updateStreamSettings() {
		try {
			await base?.applySettings({
				targetBitrate,
				maxFramerate,
				resolutionScale,
				encoderSpeed,
			});
		} catch (err) {
			error = `Failed to update settings: ${err instanceof Error ? err.message : String(err)}`;
		}
	}

	async function takeSnapshot() {
		const blob = await base?.captureSnapshot();
		if (!blob) return;
		const url = URL.createObjectURL(blob);
		const a = document.createElement("a");
		a.href = url;
		a.download = `snapshot-${Date.now()}.png`;
		a.click();
		URL.revokeObjectURL(url);
	}

	// ── Helpers ───────────────────────────────────────────────────────────────

	function formatBytes(bytes: number): string {
		if (bytes < 1024) return `${bytes} B`;
		if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(2)} KB`;
		return `${(bytes / (1024 * 1024)).toFixed(2)} MB`;
	}

	function formatBitrate(bitsPerSecond: number): string {
		if (bitsPerSecond < 1000) return `${bitsPerSecond} bps`;
		if (bitsPerSecond < 1_000_000)
			return `${(bitsPerSecond / 1000).toFixed(2)} Kbps`;
		return `${(bitsPerSecond / 1_000_000).toFixed(2)} Mbps`;
	}
</script>

<div class="camera-viewer {className}" style="width: {width}px;">
	<!-- Base component: owns all WebRTC logic, renders only <video> -->
	<div class="video-stack" style="width:{width}px; height:{height}px;">
		<CameraViewerBase
			bind:this={base}
			{serverUrl}
			bind:streamId={selectedStreamId}
			{streamFilter}
			{width}
			{height}
			bind:isStreaming
			bind:error
			bind:availableStreams
			bind:stats
		>
			{#snippet placeholder()}
				{#if !error}
					<div class="placeholder">
						<p>📡</p>
						<p>Stream not connected</p>
					</div>
				{/if}
			{/snippet}
		</CameraViewerBase>

		<!-- Overlays rendered on top of the base -->
		<div class="overlay-container">
			{#if error}
				<div class="error-message">
					<p>⚠️ {error}</p>
				</div>
			{/if}

			<div class="controls-overlay">
				{#if isStreaming}
					<button
						class="control-btn"
						onclick={() => (showSettings = !showSettings)}
						title="Stream Settings"
					>
						⚙️
					</button>
					<button
						class="control-btn"
						onclick={() => (showStats = !showStats)}
						title="Stream Statistics"
					>
						📊
					</button>
					<button
						class="control-btn"
						onclick={takeSnapshot}
						title="Take Snapshot"
					>
						📸
					</button>
				{/if}
			</div>

			{#if showStats && isStreaming}
				<div class="stats-panel">
					<h3>Stream Statistics</h3>
					<div class="stat-row">
						<span>FPS:</span>
						<span>{stats.fps}</span>
					</div>
					<div class="stat-row">
						<span>Bitrate:</span>
						<span>{formatBitrate(stats.bitrate)}</span>
					</div>
					<div class="stat-row">
						<span>Frames:</span>
						<span>{stats.framesDecoded}</span>
					</div>
					<div class="stat-row">
						<span>Received:</span>
						<span>{formatBytes(stats.bytesReceived)}</span>
					</div>
					<div class="stat-row">
						<span>Packets:</span>
						<span>{stats.packetsReceived}</span>
					</div>
					<div class="stat-row">
						<span>Lost:</span>
						<span>{stats.packetsLost}</span>
					</div>
				</div>
			{/if}

			{#if showSettings && isStreaming}
				<div class="settings-overlay">
					<div class="settings-panel-overlay">
						<div class="settings-header">
							<h3>Stream Settings</h3>
							<button
								class="close-btn"
								onclick={() => (showSettings = false)}>✕</button
							>
						</div>

						<div class="setting-group">
							<label for="bitrate">
								Target Bitrate: {(targetBitrate / 1_000_000).toFixed(1)} Mbps
							</label>
							<input
								id="bitrate"
								type="range"
								bind:value={targetBitrate}
								min="500000"
								max="10000000"
								step="100000"
							/>
							<div class="range-labels">
								<span>0.5 Mbps</span>
								<span>10 Mbps</span>
							</div>
						</div>

						<div class="setting-group">
							<label for="framerate">
								Max Framerate: {maxFramerate} fps
							</label>
							<input
								id="framerate"
								type="range"
								bind:value={maxFramerate}
								min="10"
								max="60"
								step="5"
							/>
							<div class="range-labels">
								<span>10 fps</span>
								<span>60 fps</span>
							</div>
						</div>

						<div class="setting-group">
							<label for="resolution">
								Resolution Scale: {(resolutionScale * 100).toFixed(0)}%
							</label>
							<input
								id="resolution"
								type="range"
								bind:value={resolutionScale}
								min="0.25"
								max="1.0"
								step="0.05"
							/>
							<div class="range-labels">
								<span>25%</span>
								<span>100%</span>
							</div>
						</div>

						<div class="setting-group">
							<label for="encoder-speed">
								Encoder Speed: {encoderSpeed}
								{encoderSpeed <= 4
									? "(Quality)"
									: encoderSpeed >= 12
										? "(Fast)"
										: "(Balanced)"}
							</label>
							<input
								id="encoder-speed"
								type="range"
								bind:value={encoderSpeed}
								min="0"
								max="16"
								step="1"
							/>
							<div class="range-labels">
								<span>Best Quality</span>
								<span>Fastest</span>
							</div>
						</div>

						<div class="setting-group">
							<button class="btn btn-apply" onclick={updateStreamSettings}>
								Apply Settings
							</button>
						</div>

						<div class="setting-group">
							<div class="info-label">Stream Info:</div>
							<div class="info-text">
								Stream: {selectedStreamId}<br />
								Resolution: {base?.getVideoElement()?.videoWidth ?? 0}×{base?.getVideoElement()?.videoHeight ?? 0}
							</div>
						</div>
					</div>
				</div>
			{/if}
		</div>
	</div>

	<div class="stream-selector">
		<label for="stream-select">Stream Source:</label>
		<div class="selector-controls">
			<select
				id="stream-select"
				bind:value={selectedStreamId}
				onchange={handleStreamChange}
			>
				{#if availableStreams.length === 0}
					<option value="">No streams available</option>
				{:else}
					{#each availableStreams as stream}
						<option value={stream.stream_id}>
							{stream.stream_id} ({stream.active_peers} viewer{stream.active_peers !== 1 ? "s" : ""})
						</option>
					{/each}
				{/if}
			</select>
			<button class="btn-icon" onclick={refreshStreams} title="Refresh streams">
				🔄
			</button>
		</div>
	</div>
</div>

<style>
	.camera-viewer {
		display: inline-block;
	}

	/* Grid stacks the base video and overlay on top of each other */
	.video-stack {
		display: grid;
		grid-template-columns: 1fr;
		grid-template-rows: 1fr;
	}

	.video-stack > :global(*) {
		grid-column: 1;
		grid-row: 1;
	}

	.overlay-container {
		position: relative;
		pointer-events: none;
		z-index: 1;
	}

	/* Re-enable pointer events only for interactive children */
	.overlay-container > * {
		pointer-events: auto;
	}

	.placeholder {
		text-align: center;
		color: var(--fw-text-muted, #666);
	}

	.placeholder p:first-child {
		font-size: 4rem;
		margin: 0;
	}

	.placeholder p:last-child {
		margin: 0.5rem 0 0 0;
		font-size: 1.2rem;
	}

	.error-message {
		position: absolute;
		top: 0;
		left: 0;
		right: 0;
		background: rgba(220, 38, 38, 0.9);
		color: white;
		padding: 1rem;
		text-align: center;
		z-index: 10;
	}

	.error-message p {
		margin: 0;
	}

	.controls-overlay {
		position: absolute;
		top: 1rem;
		right: 1rem;
		display: flex;
		gap: 0.5rem;
		z-index: 5;
	}

	.control-btn {
		background: rgba(0, 0, 0, 0.6);
		border: 2px solid rgba(255, 255, 255, 0.3);
		border-radius: 50%;
		width: 48px;
		height: 48px;
		font-size: 1.5rem;
		cursor: pointer;
		transition: all 0.2s;
		display: flex;
		align-items: center;
		justify-content: center;
	}

	.control-btn:hover {
		background: rgba(0, 0, 0, 0.8);
		border-color: rgba(255, 255, 255, 0.6);
		transform: scale(1.1);
	}

	.stream-selector {
		margin-top: 1rem;
		padding: 1rem;
		background: var(--fw-bg, #f9fafb);
		border-radius: 6px;
		border: 1px solid var(--fw-border, #e5e7eb);
	}

	.stream-selector label {
		display: block;
		margin-bottom: 0.5rem;
		font-weight: 600;
		color: var(--fw-text, #374151);
		font-size: 0.875rem;
	}

	.selector-controls {
		display: flex;
		gap: 0.5rem;
		align-items: center;
	}

	.selector-controls select {
		flex: 1;
		padding: 0.5rem;
		border: 1px solid var(--fw-border, #d1d5db);
		border-radius: 4px;
		font-size: 0.875rem;
		background: var(--fw-bg, white);
		color: var(--fw-text, #111827);
	}

	.selector-controls select:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}

	.btn-icon {
		padding: 0.5rem;
		background: var(--fw-bg, white);
		border: 1px solid var(--fw-border, #d1d5db);
		border-radius: 4px;
		cursor: pointer;
		font-size: 1rem;
		transition: all 0.2s;
		display: flex;
		align-items: center;
		justify-content: center;
		min-width: 40px;
		height: 40px;
		color: var(--fw-text, #111827);
	}

	.btn-icon:hover:not(:disabled) {
		background: var(--fw-bg-hover, #f3f4f6);
		border-color: var(--fw-text-muted, #9ca3af);
	}

	.btn-icon:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}

	.btn {
		flex: 1;
		padding: 0.75rem 1.5rem;
		font-size: 1rem;
		font-weight: 600;
		border: none;
		border-radius: 6px;
		cursor: pointer;
		transition: all 0.2s;
	}

	.btn:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}

	.settings-overlay {
		position: absolute;
		top: 0;
		left: 0;
		right: 0;
		bottom: 0;
		background: rgba(0, 0, 0, 0.6);
		display: flex;
		align-items: center;
		justify-content: center;
		z-index: 20;
		animation: fadeIn 0.2s ease-out;
		padding: 1rem;
	}

	@keyframes fadeIn {
		from { opacity: 0; }
		to   { opacity: 1; }
	}

	.settings-panel-overlay {
		background: var(--fw-bg-subtle, #f3f4f6);
		border-radius: 8px;
		padding: 1.5rem;
		max-width: 500px;
		width: 100%;
		max-height: 80%;
		overflow-y: auto;
		box-shadow: 0 10px 25px rgba(0, 0, 0, 0.5);
		animation: slideUp 0.3s ease-out;
	}

	@keyframes slideUp {
		from { opacity: 0; transform: translateY(20px); }
		to   { opacity: 1; transform: translateY(0);    }
	}

	.settings-header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		margin-bottom: 1rem;
	}

	.settings-header h3 {
		margin: 0;
		font-size: 1.25rem;
		color: var(--fw-text, #1f2937);
	}

	.close-btn {
		background: none;
		border: none;
		font-size: 1.5rem;
		color: var(--fw-text-muted, #6b7280);
		cursor: pointer;
		padding: 0;
		width: 32px;
		height: 32px;
		display: flex;
		align-items: center;
		justify-content: center;
		border-radius: 4px;
		transition: all 0.2s;
	}

	.close-btn:hover {
		background: var(--fw-bg-hover, #e5e7eb);
		color: var(--fw-text, #1f2937);
	}

	.setting-group {
		margin-bottom: 1.25rem;
	}

	.setting-group:last-child {
		margin-bottom: 0;
	}

	.setting-group label {
		display: block;
		margin-bottom: 0.5rem;
		font-weight: 500;
		color: var(--fw-text, #374151);
	}

	.setting-group select {
		width: 100%;
		padding: 0.5rem;
		border: 1px solid var(--fw-border, #d1d5db);
		border-radius: 4px;
		font-size: 1rem;
		background: var(--fw-bg, white);
		color: var(--fw-text, #111827);
	}

	.setting-group input[type="range"] {
		width: 100%;
		height: 8px;
		border-radius: 4px;
		outline: none;
		-webkit-appearance: none;
		appearance: none;
		background: var(--fw-border, #d1d5db);
		accent-color: var(--fw-primary, #3b82f6);
	}

	.setting-group input[type="range"]::-webkit-slider-thumb {
		-webkit-appearance: none;
		appearance: none;
		width: 20px;
		height: 20px;
		border-radius: 50%;
		background: var(--fw-primary, #3b82f6);
		cursor: pointer;
	}

	.setting-group input[type="range"]::-moz-range-thumb {
		width: 20px;
		height: 20px;
		border-radius: 50%;
		background: var(--fw-primary, #3b82f6);
		cursor: pointer;
		border: none;
	}

	.info-text {
		padding: 0.75rem;
		background: var(--fw-bg, white);
		border-radius: 4px;
		border: 1px solid var(--fw-border, #d1d5db);
		font-size: 0.875rem;
		color: var(--fw-text-muted, #6b7280);
	}

	.info-label {
		font-weight: 500;
		color: var(--fw-text, #374151);
		margin-bottom: 0.5rem;
	}

	.stats-panel {
		position: absolute;
		top: 1rem;
		left: 1rem;
		background: rgba(0, 0, 0, 0.8);
		color: white;
		padding: 1rem;
		border-radius: 6px;
		min-width: 200px;
		font-size: 0.875rem;
		backdrop-filter: blur(10px);
		z-index: 5;
	}

	.stats-panel h3 {
		margin: 0 0 0.75rem 0;
		font-size: 1rem;
		font-weight: 600;
	}

	.stat-row {
		display: flex;
		justify-content: space-between;
		padding: 0.25rem 0;
		border-bottom: 1px solid rgba(255, 255, 255, 0.1);
	}

	.stat-row:last-child {
		border-bottom: none;
	}

	.range-labels {
		display: flex;
		justify-content: space-between;
		font-size: 0.75rem;
		color: var(--fw-text-muted, #6b7280);
		margin-top: 0.25rem;
	}
</style>
