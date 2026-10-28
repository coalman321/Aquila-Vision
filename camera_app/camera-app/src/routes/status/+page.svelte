<script lang="ts">
	import { browser } from "$app/environment";
	import { onMount } from "svelte";
	import { LaunchManager } from "$framework";

	const SERVER_URL = browser ? window.location.origin : "";

	let version = $state("unknown");

	onMount(async () => {
		try {
			const res = await fetch(`${SERVER_URL}/api/version`);
			if (res.ok) version = (await res.json()).version ?? "unknown";
		} catch {
			// leave as unknown
		}
	});
</script>

<div class="panel">
	<h2>Status</h2>
	<p class="subtitle">Overview and realtime status of services and nodes</p>
	<p class="version">Version: {version}</p>
	<div class="launch-section">
		<LaunchManager serverUrl={SERVER_URL} apiEndpoint="/api/launch" />
	</div>
</div>

<style>
	.panel {
		padding: 12px 20px;
		background: #1a1a1a;
		border: 1px solid #2a2a2a;
		border-radius: 10px;
		color: #f9fafb;
	}

	h2 {
		font-size: 1.75rem;
		margin-bottom: 0.5rem;
		margin-top: 0.5rem;
		color: #f9fafb;
	}

	.subtitle {
		font-size: 0.875rem;
		color: #9ca3af;
		margin-bottom: 1rem;
	}

	.version {
		font-size: 0.875rem;
		color: #d1d5db;
		margin-bottom: 1rem;
	}

	.launch-section {
		margin-bottom: 3rem;
		padding: 1px 20px 20px 20px;
		background: #222222;
		border-radius: 12px;
		box-shadow: 0 1px 3px rgba(0, 0, 0, 0.4);
		width: fit-content;
	}

	.launch-section h2 {
		font-size: 1.75rem;
		margin-bottom: 0.5rem;
		margin-top: 0.5rem;
		color: #f9fafb;
	}
</style>
