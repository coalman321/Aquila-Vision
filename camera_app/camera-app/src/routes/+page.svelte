<script lang="ts">
	import { browser } from "$app/environment";
	import PipelineConfigurator from "$lib/components/PipelineConfigurator.svelte";

	const SERVER_URL = browser ? window.location.origin : "";

	function handleSave(event: CustomEvent) {
		console.log("Camera config saved:", event.detail.config);
	}

	function handleCalibrationUpload(event: CustomEvent) {
		console.log("Calibration uploaded:", event.detail);
	}
</script>

<div class="panel">
	<PipelineConfigurator
		serverUrl={SERVER_URL}
		on:save={handleSave}
		on:calibrationUploaded={handleCalibrationUpload}
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
</style>
