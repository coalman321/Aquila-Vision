<script lang="ts">
	import { browser } from "$app/environment";
	import LoggerManager from "$lib/components/LoggerManager.svelte";

	const SERVER_URL = browser ? window.location.origin : "";
</script>

<div class="panel">
	<LoggerManager serverUrl={SERVER_URL} />
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
