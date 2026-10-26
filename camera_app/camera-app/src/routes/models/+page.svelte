<script lang="ts">
	import { browser } from "$app/environment";
	import { FileUploader } from "$framework";

	const SERVER_URL = browser ? window.location.origin : "";

	function handleFileUploaded(event: CustomEvent) {
		console.log("File uploaded:", event.detail);
	}

	function handleFileRemoved(event: CustomEvent) {
		console.log("File removed:", event.detail);
	}
</script>

<div class="panel">
	<h2>Models</h2>
	<p class="subtitle">Upload and manage ML models used for object detection pipelines.</p>
	<FileUploader
		serverUrl={SERVER_URL}
		apiEndpoint={SERVER_URL + "/api/upload/models"}
		hintText="Upload model files (any type)"
		on:uploaded={handleFileUploaded}
		on:removed={handleFileRemoved}
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
