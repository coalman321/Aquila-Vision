<script lang="ts">
	import { onMount, onDestroy, tick, untrack } from "svelte";
	import type { Snippet } from "svelte";

	// ── Public types ─────────────────────────────────────────────────────────

	export interface StreamInfo {
		stream_id: string;
		active_peers: number;
	}

	/**
	 * Restricts which streams are offered. A string (or list of strings) is a
	 * topic namespace: "/camera_cal" matches "/camera_cal/debug_image" but not
	 * "/camera_cal2/image". A function receives the stream ID and returns
	 * whether to keep it.
	 */
	export type StreamFilter = string | string[] | ((streamId: string) => boolean);

	export interface StreamSettings {
		targetBitrate?: number;  // bits/s, e.g. 2_000_000
		maxFramerate?: number;   // fps, e.g. 30
		resolutionScale?: number; // 0.25–1.0
		encoderSpeed?: number;   // 0 (best quality) – 16 (fastest)
	}

	export interface StreamStats {
		bytesReceived: number;
		packetsReceived: number;
		packetsLost: number;
		framesDecoded: number;
		fps: number;
		bitrate: number;  // bits/s
	}

	// ── Props ─────────────────────────────────────────────────────────────────

	interface Props {
		/** Base URL of the WebRTC signalling server */
		serverUrl?: string;
		/** Stream ID to connect to. Bindable – updated when auto-selected. */
		streamId?: string;
		width?: number;
		height?: number;
		className?: string;
		/** Automatically start the stream after mount */
		autostart?: boolean;
		/** Only offer streams matching this filter (all streams when omitted) */
		streamFilter?: StreamFilter;
		/** Reactive output: whether video is currently flowing */
		isStreaming?: boolean;
		/** Reactive output: last error message, or null when clear */
		error?: string | null;
		/** Reactive output: list of streams reported by the server */
		availableStreams?: StreamInfo[];
		/** Reactive output: latest RTP statistics (updated every second) */
		stats?: StreamStats;
		/** Rendered in place of the video while it isn't streaming */
		placeholder?: Snippet;
	}

	let {
		serverUrl = "http://localhost:8080",
		streamId = $bindable(""),
		width = 640,
		height = 480,
		className = "",
		autostart = true,
		streamFilter,
		isStreaming = $bindable(false),
		error = $bindable<string | null>(null),
		availableStreams = $bindable<StreamInfo[]>([]),
		stats = $bindable<StreamStats>({
			bytesReceived: 0,
			packetsReceived: 0,
			packetsLost: 0,
			framesDecoded: 0,
			fps: 0,
			bitrate: 0,
		}),
		placeholder,
	}: Props = $props();

	// ── Internal state ────────────────────────────────────────────────────────

	let videoElement = $state<HTMLVideoElement | null>(null);
	let peerConnection: RTCPeerConnection | null = null;
	let peerId: string | null = null;
	let statsInterval: number | null = null;
	// Unfiltered list from the server; `availableStreams` is the filtered view.
	let allStreams: StreamInfo[] = [];

	// ── Public API ────────────────────────────────────────────────────────────

	/**
	 * Fetch the list of available streams from the server and return them.
	 * Also updates the `availableStreams` bindable prop.
	 */
	export async function queryAvailableStreams(): Promise<StreamInfo[]> {
		await _loadStreams();
		return availableStreams;
	}

	/**
	 * Connect to the given stream (or the current `streamId` prop if omitted).
	 * Stops any existing connection first.
	 */
	export async function connect(id?: string): Promise<void> {
		if (id !== undefined) streamId = id;
		await _startStream();
	}

	/**
	 * Disconnect from the current stream and notify the server.
	 */
	export async function disconnect(): Promise<void> {
		await _stopStream();
	}

	/**
	 * Push new encoding/bandwidth settings to the server for this peer.
	 * Only valid while streaming; throws if not connected.
	 */
	export async function applySettings(settings: StreamSettings): Promise<void> {
		if (!isStreaming || !peerId) {
			throw new Error("Cannot apply settings: not currently streaming");
		}
		await _applySettings(settings);
	}

	/**
	 * Capture the current video frame and return it as a PNG Blob.
	 * Returns null if the stream is not active.
	 */
	export function captureSnapshot(): Promise<Blob | null> {
		return _captureSnapshot();
	}

	/**
	 * Return a snapshot of the current RTP statistics object.
	 */
	export function getStats(): StreamStats {
		return { ...stats };
	}

	/**
	 * Expose the underlying <video> element for advanced use-cases
	 * (e.g. fullscreen, custom overlays).
	 */
	export function getVideoElement(): HTMLVideoElement | null {
		return videoElement;
	}

	// ── Private implementation ────────────────────────────────────────────────

	async function _loadStreams(): Promise<void> {
		try {
			const response = await fetch(`${serverUrl}/webrtc/streams`);
			if (!response.ok) throw new Error(`HTTP ${response.status}`);
			const data = await response.json();
			allStreams = data.streams ?? [];
			_applyFilter();
		} catch (err) {
			error = `Failed to load streams: ${err instanceof Error ? err.message : String(err)}`;
		}
	}

	function _matchesFilter(id: string): boolean {
		if (!streamFilter) return true;
		if (typeof streamFilter === "function") return streamFilter(id);
		const namespaces = Array.isArray(streamFilter) ? streamFilter : [streamFilter];
		return namespaces.some((ns) => {
			const prefix = ns.replace(/\/+$/, "");
			return prefix === "" || id === prefix || id.startsWith(`${prefix}/`);
		});
	}

	/**
	 * Recompute `availableStreams` from the server list. If the selected stream
	 * is excluded by the filter, select the first matching one instead.
	 * Returns true when the selection changed.
	 */
	function _applyFilter(): boolean {
		availableStreams = allStreams.filter((s) => _matchesFilter(s.stream_id));
		if (streamId && _matchesFilter(streamId)) return false;
		const next = availableStreams[0]?.stream_id ?? "";
		if (next === streamId) return false;
		streamId = next;
		return true;
	}

	async function _startStream(): Promise<void> {
		try {
			error = null;

			if (!streamId) {
				error = "No stream selected";
				return;
			}

			if (peerConnection) await _stopStream();

			const pc = new RTCPeerConnection({
				// No STUN needed for same-LAN use; removing the Google STUN
				// server avoids blocking on an external STUN round-trip.
				iceServers: [],
				// Force all media through a single transport (no BUNDLE negotiation delay).
				bundlePolicy: "max-bundle",
				// Only use local network paths — no relay candidates.
				iceTransportPolicy: "all",
			});

			// Request minimum playout delay from the browser jitter buffer.
			// Default is ~150ms; 0 tells the browser to play as soon as a
			// frame is decodable without waiting for jitter smoothing.
			const transceiver = pc.addTransceiver("video", { direction: "recvonly" });
			if ("playoutDelayHint" in transceiver.receiver) {
				(transceiver.receiver as any).playoutDelayHint = 0;
			}

			pc.ontrack = async (event) => {
				if (videoElement) {
					videoElement.srcObject = event.streams[0];
					// Prevent the browser from buffering for remote playback
					// or applying its own latency-smoothing policy.
					videoElement.playsInline = true;
					videoElement.disableRemotePlayback = true;
					// Set isStreaming before tick() so the browser sees an
					// unhidden element by the time play() is called.
					isStreaming = true;
					await tick();
					videoElement.play().catch((e) =>
						console.warn("video.play() rejected:", e),
					);
				}
			};

			pc.onconnectionstatechange = () => {
				if (
					pc.connectionState === "failed" ||
					pc.connectionState === "disconnected"
				) {
					error = "Connection lost";
					isStreaming = false;
				}
			};

			const offer = await pc.createOffer();
			await pc.setLocalDescription(offer);

			const response = await fetch(`${serverUrl}/webrtc/offer`, {
				method: "POST",
				headers: { "Content-Type": "application/json" },
				body: JSON.stringify({
					type: offer.type,
					sdp: offer.sdp,
					stream_id: streamId,
				}),
			});

			if (!response.ok) {
				throw new Error(
					`HTTP ${response.status}: ${await response.text()}`,
				);
			}

			const answer = await response.json();
			await pc.setRemoteDescription({ type: answer.type, sdp: answer.sdp });

			peerConnection = pc;
			peerId = answer.peer_id;
			_startStatsCollection();
		} catch (err) {
			error = `Failed to start stream: ${err instanceof Error ? err.message : String(err)}`;
			isStreaming = false;
			peerConnection?.close();
			peerConnection = null;
		}
	}

	async function _stopStream(): Promise<void> {
		_stopStatsCollection();
		peerConnection?.close();
		peerConnection = null;

		if (peerId) {
			try {
				await fetch(`${serverUrl}/webrtc/disconnect`, {
					method: "POST",
					headers: { "Content-Type": "application/json" },
					body: JSON.stringify({ peer_id: peerId }),
				});
			} catch {
				// Best-effort disconnect; ignore network errors on teardown.
			}
			peerId = null;
		}

		if (videoElement) videoElement.srcObject = null;
		isStreaming = false;
	}

	async function _applySettings(settings: StreamSettings): Promise<void> {
		const payload: Record<string, unknown> = { peer_id: peerId };
		if (settings.targetBitrate !== undefined)
			payload.target_bitrate = settings.targetBitrate;
		if (settings.maxFramerate !== undefined)
			payload.max_framerate = settings.maxFramerate;
		if (settings.resolutionScale !== undefined)
			payload.resolution_scale = settings.resolutionScale;
		if (settings.encoderSpeed !== undefined)
			payload.encoder_speed = settings.encoderSpeed;

		const response = await fetch(`${serverUrl}/webrtc/bandwidth/peer`, {
			method: "POST",
			headers: { "Content-Type": "application/json" },
			body: JSON.stringify(payload),
		});

		if (!response.ok) {
			throw new Error(`HTTP ${response.status}`);
		}
		error = null;
	}

	function _captureSnapshot(): Promise<Blob | null> {
		return new Promise((resolve) => {
			if (!videoElement || !isStreaming) {
				resolve(null);
				return;
			}
			const canvas = document.createElement("canvas");
			canvas.width = videoElement.videoWidth;
			canvas.height = videoElement.videoHeight;
			const ctx = canvas.getContext("2d");
			if (!ctx) {
				resolve(null);
				return;
			}
			ctx.drawImage(videoElement, 0, 0);
			canvas.toBlob((blob) => resolve(blob), "image/png");
		});
	}

	function _startStatsCollection(): void {
		if (statsInterval) return;

		let lastFramesDecoded = 0;
		let lastBytesReceived = 0;
		let lastTimestamp = Date.now();

		statsInterval = window.setInterval(async () => {
			if (!peerConnection || !isStreaming) return;
			try {
				const report = await peerConnection.getStats();
				let inbound: RTCInboundRtpStreamStats | null = null;
				for (const entry of report.values()) {
					if (entry.type === "inbound-rtp" && (entry as RTCInboundRtpStreamStats).kind === "video") {
						inbound = entry as RTCInboundRtpStreamStats;
					}
				}
				if (!inbound) return;

				const now = Date.now();
				const dt = (now - lastTimestamp) / 1000;
				const frames = inbound.framesDecoded ?? 0;
				const bytes = inbound.bytesReceived ?? 0;

				stats = {
					bytesReceived: bytes,
					packetsReceived: inbound.packetsReceived ?? 0,
					packetsLost: inbound.packetsLost ?? 0,
					framesDecoded: frames,
					fps: dt > 0 ? Math.round((frames - lastFramesDecoded) / dt) : 0,
					bitrate:
						dt > 0
							? Math.round(((bytes - lastBytesReceived) * 8) / dt)
							: 0,
				};

				lastFramesDecoded = frames;
				lastBytesReceived = bytes;
				lastTimestamp = now;
			} catch (e) {
				console.error("Stats collection error:", e);
			}
		}, 1000);
	}

	function _stopStatsCollection(): void {
		if (statsInterval !== null) {
			clearInterval(statsInterval);
			statsInterval = null;
		}
	}

	// ── Lifecycle ─────────────────────────────────────────────────────────────

	onMount(() => {
		// The cleanup has to be returned synchronously; Svelte ignores one from an async callback.
		(async () => {
			await _loadStreams();
			if (autostart && streamId) await _startStream();
		})();

		const refreshTimer = setInterval(_loadStreams, 5000);
		return () => clearInterval(refreshTimer);
	});

	// Re-filter when the filter prop changes; follow the selection if it moved
	// off a stream that was playing.
	let filterInitialized = false;
	$effect(() => {
		streamFilter;
		untrack(() => {
			if (!filterInitialized) {
				filterInitialized = true;
				return;
			}
			const wasStreaming = isStreaming;
			if (_applyFilter()) {
				if (wasStreaming && streamId) _startStream();
				else if (!streamId) _stopStream();
			}
		});
	});

	onDestroy(() => {
		_stopStream();
	});
</script>

<!--
  Renders only the <video> element. All controls are intentionally omitted –
  use the exported functions (connect, disconnect, applySettings, …) to drive
  the component from a parent or wrapper component.
-->
<div class="camera-viewer-base {className}" style="width:{width}px; height:{height}px;">
	<video
		bind:this={videoElement}
		class:hidden={!isStreaming}
		style="width:{width}px; height:{height}px;"
		autoplay
		playsinline
		muted
	></video>

	{#if !isStreaming}
		<!-- Optional placeholder snippet supplied by the parent -->
		{@render placeholder?.()}
	{/if}
</div>

<style>
	.camera-viewer-base {
		position: relative;
		background: #000;
		border-radius: 8px;
		overflow: hidden;
		display: flex;
		align-items: center;
		justify-content: center;
	}

	video {
		display: block;
		width: 100%;
		height: 100%;
		object-fit: contain;
	}

	video.hidden {
		display: none;
	}
</style>
