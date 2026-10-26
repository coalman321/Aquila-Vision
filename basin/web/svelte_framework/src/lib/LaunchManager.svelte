<script lang="ts">
	import { onMount, onDestroy, tick } from "svelte";

	// Props
	let {
		serverUrl = "http://localhost:8080",
		apiEndpoint = "/launch",
		pollInterval = 1000,
		className = "",
	}: {
		serverUrl?: string;
		apiEndpoint?: string;
		pollInterval?: number;
		className?: string;
	} = $props();

	// Types based on the C++ ProcessList structure
	import type {
		Process,
		SystemProcessList,
		ProcessListResponse,
		RemoteRequest,
		RemoteResponse,
	} from "./apiInterfaces.js";
	import {
		REQUEST_START,
		REQUEST_STOP,
		REQUEST_RESTART,
		REQUEST_LOAD_CONFIG,
		REQUEST_LAUNCH,
		REQUEST_SHUTDOWN,
		PROCESS_STATUS_NAMES,
		RESPONSE_OK,
		RESPONSE_NAMES,
	} from "./apiInterfaces.js";

	// Generic remote request helpers below

	// State
	let processLists = $state<ProcessListResponse>({});
	// Toasts for error/success feedback
	let toasts = $state<Array<{ id: number; message: string; type?: string }>>(
		[],
	);
	let toastDuration = $state<number>(5000);
	let _nextToastId = 1;
	let loading = $state(true);
	let pollTimer: number | null = null;

	// Action tracking (per-process / per-system)
	let actionInProgress = $state<Record<string, boolean>>({});

	// Load config modal state
	let modalVisible = $state(false);
	let modalSystemName = $state<string | null>(null);
	let modalSystemData = $state<SystemProcessList | null>(null);
	let modalInput = $state<string>("");
	let modalInputEl = $state<HTMLInputElement | null>(null);

	// Load-config modal state used to enter scenario arguments

	/**
	 * Fetch process list from the server
	 */
	async function fetchProcessList() {
		try {
			const response = await fetch(`${serverUrl}${apiEndpoint}/list`);
			if (!response.ok) {
				throw new Error(`HTTP error! status: ${response.status}`);
			}
			const data = await response.json();
			processLists = data ?? {};
			hideError();
			loading = false;
		} catch (err) {
			showError(
				err instanceof Error ? err.message : "Unknown error occurred",
			);
			loading = false;
		}
	}

	/**
	 * Generic helper used to send a remote request and track per-action state
	 */
	async function sendRemoteRequest(
		systemName: string,
		requestId: number,
		idArg: number,
		scenarioArg: string,
		actionKey?: string,
	) {
		const key = actionKey ?? `${systemName}-${requestId}-${idArg}-${scenarioArg}`;
		if (actionInProgress[key]) return;
		// mark as in-progress
		actionInProgress = { ...actionInProgress, [key]: true };

		// no UI feedback state tracked here; toasts will show messages
		try {
			const requestBody: RemoteRequest = {
				system_name: systemName,
				request_id: Number(requestId),
				id_argument: idArg,
				scenario_argument: scenarioArg,
			};

			const response = await fetch(`${serverUrl}${apiEndpoint}/request`, {
				method: "POST",
				headers: {
					"Content-Type": "application/json",
				},
				body: JSON.stringify(requestBody),
			});

			// Always attempt to parse JSON first; prefer to check resp_code from the RPC response
			let parsed: RemoteResponse | null = null;
			try {
				parsed = await response.json();
			} catch (e) {
				parsed = null;
			}

			if (parsed !== null) {
				if (
					parsed.resp_code !== undefined &&
					parsed.resp_code !== RESPONSE_OK
				) {
					const name =
						RESPONSE_NAMES[parsed.resp_code] ??
						String(parsed.resp_code);
					const message = parsed.response_msg || `${name}`;
					throw new Error(message);
				}
				hideError();
			} else {
				// Not JSON; fallback to HTTP status if not ok
				if (!response.ok) {
					const msg = `HTTP error! status: ${response.status} ${response.statusText}`;
					throw new Error(msg);
				}
				hideError();
			}
		} catch (err) {
			showError(
				err instanceof Error ? err.message : "Unknown error occurred",
			);
		} finally {
			actionInProgress = { ...actionInProgress, [key]: false };
		}
	}

	// Per-process helpers
	async function sendProcessAction(systemName: string, process: Process, requestType: number) {
		const key = `process-${process.process_id}-${requestType}`;
		await sendRemoteRequest(systemName, requestType, process.process_id, "", key);
	}

	// Per-system helpers
	async function sendSystemAction(
		systemName: string,
		systemData: SystemProcessList,
		requestType: number,
		scenarioArg?: string,
	) {
		const key = `system-${systemName}-${requestType}`;
		// Determine scenario argument preference: passed-in value > stored active_config for LOAD_CONFIG/LAUNCH > empty
		let effectiveScenario = "";
		if (typeof scenarioArg === "string" && scenarioArg.length > 0) {
			effectiveScenario = scenarioArg;
		} else if (
			requestType === REQUEST_LOAD_CONFIG ||
			requestType === REQUEST_LAUNCH
		) {
			effectiveScenario = systemData.active_config || "";
		}
		await sendRemoteRequest(systemName, requestType, 0, effectiveScenario, key);
	}

	// Flow control for prompting the user for a scenario string when loading a config
	async function initiateLoadConfig(
		systemName: string,
		systemData: SystemProcessList,
	) {
		modalVisible = true;
		modalSystemName = systemName;
		modalSystemData = systemData;
		modalInput = systemData.active_config ?? "";
		await tick();
		modalInputEl?.focus();
	}

	function cancelLoadConfig() {
		modalVisible = false;
		modalSystemName = null;
		modalSystemData = null;
		modalInput = "";
	}

	async function confirmLoadConfig() {
		if (!modalSystemName || !modalSystemData) return;
		const val = modalInput ?? modalSystemData.active_config ?? "";
		modalVisible = false;
		// call the system action with the user-specified scenario arg
		await sendSystemAction(
			modalSystemName,
			modalSystemData,
			REQUEST_LOAD_CONFIG,
			val,
		);
		modalSystemName = null;
		modalSystemData = null;
		modalInput = "";
	}

	// Error helpers (toast-only)
	function showError(msg: string) {
		console.error("LaunchManager showError:", msg);
		addToast(msg, "error");
	}

	function hideError() {
		clearToasts();
	}

	function addToast(message: string, type = "error") {
		const id = _nextToastId++;
		toasts = [...toasts, { id, message, type }];
		// Auto-dismiss after duration
		setTimeout(() => {
			removeToast(id);
		}, toastDuration);
		return id;
	}

	function removeToast(id: number) {
		toasts = toasts.filter((t) => t.id !== id);
	}

	function clearToasts() {
		toasts = [];
	}
	/**
	 * Start polling for process list updates
	 */
	function startPolling() {
		fetchProcessList(); // Initial fetch
		pollTimer = window.setInterval(() => {
			fetchProcessList();
		}, pollInterval);
	}

	/**
	 * Stop polling
	 */
	function stopPolling() {
		if (pollTimer !== null) {
			clearInterval(pollTimer);
			pollTimer = null;
		}
	}

	/**
	 * Format memory in human-readable format
	 */
	function formatMemory(bytes: number): string {
		if (bytes < 1024) return `${bytes} B`;
		if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(1)} KB`;
		if (bytes < 1024 * 1024 * 1024)
			return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
		return `${(bytes / (1024 * 1024 * 1024)).toFixed(1)} GB`;
	}

	/**
	 * Get status badge color
	 */
	function getStatusColor(status: string | number): string {
		let label: string;
		if (typeof status === "number") {
			label = PROCESS_STATUS_NAMES[status] || String(status);
		} else {
			label = status;
		}
		const lowerStatus = label.toLowerCase();
		if (lowerStatus.includes("run")) return "bg-green-500";
		if (lowerStatus.includes("start") || lowerStatus.includes("restart"))
			return "bg-yellow-500";
		if (
			lowerStatus.includes("stop") ||
			lowerStatus.includes("dead") ||
			lowerStatus.includes("timeout") ||
			lowerStatus.includes("invalid") ||
			lowerStatus.includes("inactive")
		)
			return "bg-red-500";
		return "bg-gray-500";
	}

	function getStatusLabel(status: string | number): string {
		if (typeof status === "number") {
			return PROCESS_STATUS_NAMES[status] || String(status);
		}
		return status;
	}

	onMount(() => {
		startPolling();
	});

	onDestroy(() => {
		stopPolling();
	});
</script>

<div class="launch-manager {className}">
	<div class="header"></div>

	<!-- Toast container -->
	<div class="toast-container" aria-live="polite" aria-atomic="true">
		{#each toasts as toast (toast.id)}
			<div class="toast {toast.type}" role="status">
				<div class="toast-body">{toast.message}</div>
				<button
					class="btn btn-secondary toast-close"
					onclick={() => removeToast(toast.id)}
					aria-label="Close">×</button
				>
			</div>
		{/each}
	</div>

	{#if loading}
		<div class="loading">Loading process lists...</div>
	{:else if Object.keys(processLists ?? {}).length === 0}
		<div class="empty-state">No process lists available</div>
	{:else}
		<div class="systems-container">
			{#each Object.entries(processLists ?? {}) as [systemName, systemData]}
				<div class="system-card">
					<div class="system-header">
						<h3>{systemData.system_name}</h3>
						<div class="system-header-actions">
							<span class="config-badge"
								>Config: {systemData.active_config}</span
							>
							<div class="system-actions">
								<button
									class="btn btn-primary"
									onclick={() =>
										sendSystemAction(
											systemName,
											systemData,
											REQUEST_LAUNCH,
										)}
									disabled={actionInProgress[
										`system-${systemName}-` + REQUEST_LAUNCH
									]}>Launch</button
								>
								<button
									class="btn btn-secondary"
									onclick={() =>
										sendSystemAction(
											systemName,
											systemData,
											REQUEST_SHUTDOWN,
										)}
									disabled={actionInProgress[
										`system-${systemName}-` +
											REQUEST_SHUTDOWN
									]}>Shutdown</button
								>
								<button
									class="btn btn-secondary"
									onclick={() =>
										initiateLoadConfig(
											systemName,
											systemData,
										)}
									disabled={actionInProgress[
										`system-${systemName}-` +
											REQUEST_LOAD_CONFIG
									]}>Load Config</button
								>
							</div>
						</div>
						<!-- Modal overlay for entering scenario argument -->
						{#if modalVisible}
							<div
								class="modal-overlay"
								role="button"
								tabindex="0"
								onclick={() => cancelLoadConfig()}
								onkeydown={(e) =>
									(e.key === "Enter" || e.key === "Escape") &&
									cancelLoadConfig()}
							>
								<div
									class="modal"
									role="dialog"
									aria-modal="true"
									tabindex="0"
									onclick={(e) => e.stopPropagation()}
									onkeydown={(e) => {
										if (e.key === "Escape")
											cancelLoadConfig();
										if (e.key === "Enter")
											confirmLoadConfig();
									}}
								>
									<div class="modal-header">
										<h3>
											Load Config: {modalSystemData?.system_name ??
												modalSystemName}
										</h3>
									</div>
									<div class="modal-body">
										<label for="scenario-input"
											>Scenario argument</label
										>
										<input
											id="scenario-input"
											type="text"
											class="load-config-input"
											bind:value={modalInput}
											placeholder="Enter scenario string (optional)"
											bind:this={modalInputEl}
										/>
									</div>
									<div class="modal-actions">
										<button
											class="btn btn-primary"
											onclick={() => confirmLoadConfig()}
											disabled={actionInProgress[
												`system-${modalSystemName ?? ""}-` +
													REQUEST_LOAD_CONFIG
											]}>Confirm</button
										>
										<button
											class="btn btn-secondary"
											onclick={() => cancelLoadConfig()}
											>Cancel</button
										>
									</div>
								</div>
							</div>
						{/if}
					</div>

					{#if systemData.processes.length === 0}
						<div class="empty-processes">No processes running</div>
					{:else}
						<div class="processes-table">
							<table>
								<thead>
									<tr>
										<th>ID</th>
										<th>Name</th>
										<th>Status</th>
										<th>PID</th>
										<th>CPU %</th>
										<th>Memory</th>
										<th>Restarts</th>
										<th>Actions</th>
									</tr>
								</thead>
								<tbody>
									{#each systemData.processes as process}
										<tr>
											<td>{process.process_id}</td>
											<td class="process-name"
												>{process.process_name}</td
											>
											<td>
												<span
													class="status-badge {getStatusColor(
														process.process_status,
													)}"
												>
													{getStatusLabel(
														process.process_status,
													)}
												</span>
											</td>
											<td>{process.local_pid}</td>
											<td
												>{(process.process_cpu_pct * 100).toFixed(
													1,
												)}%</td
											>
											<td
												>{formatMemory(
													process.process_memory,
												)}</td
											>
											<td>{process.restart_counter}</td>
											<td>
												<div class="button-group">
													<button
														class="btn btn-primary btn-icon"
														onclick={() =>
															sendProcessAction(
																systemName,
																process,
																REQUEST_START,
															)}
														disabled={actionInProgress[
															`process-${process.process_id}-${REQUEST_START}`
														]}
														aria-label="Start process"
														title="Start"
													>
														<svg
															viewBox="0 0 24 24"
															fill="currentColor"
															aria-hidden="true"
															><path
																d="M8 5v14l11-7z"
															></path></svg
														>
														<span class="sr-only"
															>Start</span
														>
													</button>

													<button
														class="btn btn-secondary btn-icon"
														onclick={() =>
															sendProcessAction(
																systemName,
																process,
																REQUEST_STOP,
															)}
														disabled={actionInProgress[
															`process-${process.process_id}-${REQUEST_STOP}`
														]}
														aria-label="Stop process"
														title="Stop"
													>
														<svg
															viewBox="0 0 24 24"
															fill="currentColor"
															aria-hidden="true"
															><path
																d="M6 6h12v12H6z"
															></path></svg
														>
														<span class="sr-only"
															>Stop</span
														>
													</button>

													<button
														class="btn btn-secondary btn-icon"
														onclick={() =>
															sendProcessAction(
																systemName,
																process,
																REQUEST_RESTART,
															)}
														disabled={actionInProgress[
															`process-${process.process_id}-${REQUEST_RESTART}`
														]}
														aria-label="Restart process"
														title="Restart"
													>
														<svg
															viewBox="0 0 24 24"
															fill="currentColor"
															aria-hidden="true"
															><path
																d="M12 6V4l4 4-4 4V8a4 4 0 1 0 4 4h2a6 6 0 1 1-6-6z"
															></path></svg
														>
														<span class="sr-only"
															>Restart</span
														>
													</button>
												</div>
											</td>
										</tr>
									{/each}
								</tbody>
							</table>
						</div>
					{/if}
				</div>
			{/each}
		</div>
	{/if}
</div>

<style>
	.launch-manager {
		font-family:
			system-ui,
			-apple-system,
			sans-serif;
		padding: 1rem;
		max-width: 100%;
	}

	.header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		margin-bottom: 1.5rem;
	}

	.btn {
		padding: 0.5rem 1rem;
		border: none;
		border-radius: 0.375rem;
		font-size: 0.875rem;
		font-weight: 500;
		cursor: pointer;
		transition: all 0.2s;
	}

	.btn:disabled {
		opacity: 0.5;
		cursor: not-allowed;
	}

	.btn-primary {
		background-color: var(--fw-primary, #3b82f6);
		color: white;
	}

	.btn-primary:hover:not(:disabled) {
		background-color: var(--fw-primary-hover, #2563eb);
	}

	.btn-secondary {
		background-color: var(--fw-secondary, #6b7280);
		color: white;
	}

	.btn-secondary:hover:not(:disabled) {
		background-color: var(--fw-secondary-hover, #4b5563);
	}

	/* Toaster-based inline error/warning display */

	.loading,
	.empty-state {
		text-align: center;
		padding: 2rem;
		color: var(--fw-text-muted, #6b7280);
	}

	.systems-container {
		display: flex;
		flex-direction: column;
		gap: 1.5rem;
	}

	.system-card {
		background-color: var(--fw-bg, white);
		border: 1px solid var(--fw-border, #e5e7eb);
		border-radius: 0.5rem;
		padding: 1.5rem;
		box-shadow: 0 1px 3px rgba(0, 0, 0, 0.3);
	}

	.system-header {
		display: flex;
		justify-content: space-between;
		align-items: center;
		margin-bottom: 1rem;
		padding-bottom: 1rem;
		border-bottom: 1px solid var(--fw-border, #e5e7eb);
	}

	.system-header-actions {
		display: flex;
		gap: 0.75rem;
		align-items: center;
	}

	.system-actions {
		display: flex;
		gap: 0.5rem;
	}

	.system-header h3 {
		margin: 0;
		font-size: 1.25rem;
		font-weight: 600;
	}

	.config-badge {
		background-color: var(--fw-badge-bg, #dbeafe);
		color: var(--fw-badge-text, #1e40af);
		padding: 0.25rem 0.75rem;
		border-radius: 9999px;
		font-size: 0.75rem;
		font-weight: 500;
	}

	.empty-processes {
		text-align: center;
		padding: 1.5rem;
		color: var(--fw-text-muted, #9ca3af);
		font-style: italic;
	}

	.processes-table {
		overflow-x: auto;
	}

	table {
		width: 100%;
		border-collapse: collapse;
		font-size: 0.875rem;
	}

	thead {
		background-color: var(--fw-bg-subtle, #f9fafb);
	}

	th {
		text-align: left;
		padding: 0.75rem 0.5rem;
		font-weight: 600;
		color: var(--fw-text, #374151);
		border-bottom: 2px solid var(--fw-border, #e5e7eb);
	}

	td {
		padding: 0.75rem 0.5rem;
		border-bottom: 1px solid var(--fw-border, #e5e7eb);
	}

	.process-name {
		font-weight: 500;
	}

	.status-badge {
		display: inline-block;
		padding: 0.25rem 0.5rem;
		border-radius: 0.25rem;
		font-size: 0.75rem;
		font-weight: 500;
		color: white;
	}

	.bg-green-500 {
		background-color: #10b981;
	}

	.bg-red-500 {
		background-color: #ef4444;
	}

	.bg-yellow-500 {
		background-color: #f59e0b;
	}

	.bg-gray-500 {
		background-color: #6b7280;
	}

	tbody tr:hover {
		background-color: var(--fw-bg-subtle, #f9fafb);
	}

	.button-group {
		display: flex;
		gap: 0.5rem;
		align-items: center;
	}

	/* Icon-only buttons */
	.btn-icon {
		padding: 0.25rem;
		display: inline-flex;
		justify-content: center;
		align-items: center;
		width: 2.25rem;
		height: 2.25rem;
	}
	.btn-icon svg {
		width: 1.25rem;
		height: 1.25rem;
		display: block;
	}
	.sr-only {
		position: absolute !important;
		width: 1px !important;
		height: 1px !important;
		padding: 0 !important;
		margin: -1px !important;
		overflow: hidden !important;
		clip: rect(0, 0, 0, 0) !important;
		white-space: nowrap !important;
		border: 0 !important;
	}

	/* Modal styles */
	.modal-overlay {
		position: fixed;
		inset: 0;
		background-color: rgba(0, 0, 0, 0.5);
		display: flex;
		align-items: center;
		justify-content: center;
		z-index: 1000;
	}

	/* Toast styles */
	.toast-container {
		position: fixed;
		top: 1rem;
		right: 1rem;
		z-index: 100000;
		display: flex;
		flex-direction: column;
		gap: 0.5rem;
		align-items: flex-end;
	}
	.toast {
		background-color: rgba(17, 24, 39, 0.95);
		color: #fff;
		padding: 0.75rem 1rem;
		border-radius: 0.5rem;
		box-shadow: 0 6px 18px rgba(0, 0, 0, 0.2);
		min-width: 240px;
		display: flex;
		align-items: center;
		gap: 0.5rem;
	}
	.toast.error {
		background-color: #ef4444;
	}
	.toast.success {
		background-color: #10b981;
	}
	.toast.info {
		background-color: rgba(17, 24, 39, 0.95);
	}
	.toast-body {
		flex: 1;
	}
	.toast-close {
		background: transparent;
		color: #fff;
		border: none;
		cursor: pointer;
		font-size: 1.2rem;
		line-height: 1;
	}
	.modal {
		background-color: var(--fw-bg, white);
		color: var(--fw-text, #111827);
		border-radius: 0.5rem;
		padding: 1rem;
		width: 90%;
		max-width: 480px;
		box-shadow: 0 10px 30px rgba(0, 0, 0, 0.5);
	}
	.modal-header h3 {
		margin: 0 0 0.5rem 0;
	}
	.modal-body {
		display: flex;
		flex-direction: column;
		gap: 0.5rem;
	}
	.modal-actions {
		display: flex;
		gap: 0.5rem;
		margin-top: 0.75rem;
		justify-content: flex-end;
	}
	.load-config-input {
		padding: 0.5rem;
		border: 1px solid var(--fw-border, #e5e7eb);
		border-radius: 0.375rem;
		background: var(--fw-bg, white);
		color: var(--fw-text, #111827);
	}
</style>
