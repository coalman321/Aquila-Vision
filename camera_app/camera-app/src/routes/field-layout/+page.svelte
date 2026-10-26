<script lang="ts">
	import { browser } from "$app/environment";
	import FieldMapManager from "$lib/components/FieldMapManager.svelte";

	const SERVER_URL = browser ? window.location.origin : "";

	function handlePositionSaved(event: CustomEvent) {
		console.log("Position saved:", event.detail);
	}

	function handlePositionDeleted(event: CustomEvent) {
		console.log("Position deleted:", event.detail);
	}

	function handlePositionsLoaded(event: CustomEvent) {
		console.log("Positions loaded:", event.detail);
	}
</script>

<div class="panel">
	<h2>Field Layout</h2>
	<p class="subtitle">Configure field positions and mapping</p>
	<FieldMapManager
		serverUrl={SERVER_URL}
		on:positionSaved={handlePositionSaved}
		on:positionDeleted={handlePositionDeleted}
		on:positionsLoaded={handlePositionsLoaded}
	/>
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
</style>
