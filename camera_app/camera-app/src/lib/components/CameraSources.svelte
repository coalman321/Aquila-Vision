<script lang="ts">
    import { onMount } from "svelte";

    export let serverUrl: string = "http://localhost:8080";
    export let apiEndpoint: string = "/api/sources";
    export let className: string = "";

    interface SourceKey {
        camera_type: string;
        camera_name: string;
    }

    let sources: SourceKey[] = [];
    let selected: SourceKey | null = null;
    let loading = true;
    let saving = false;
    let toasts: Array<{ id: number; message: string; type?: string }> = [];
    let _nextToastId = 1;

    let details: Record<string, any> | null = null;

    function sanitizeForId(s: string) {
        return s ? String(s).replace(/[^a-zA-Z0-9_-]/g, "_") : "";
    }

    function sourceKeyToString(key: SourceKey): string {
        return `${key.camera_type}:${key.camera_name}`;
    }

    function sourceKeysEqual(a: SourceKey | null, b: SourceKey | null): boolean {
        if (!a || !b) return a === b;
        return a.camera_type === b.camera_type && a.camera_name === b.camera_name;
    }

    function addToast(message: string, type = "error") {
        const id = _nextToastId++;
        toasts = [...toasts, { id, message, type }];
        setTimeout(() => removeToast(id), 4000);
        return id;
    }

    function removeToast(id: number) {
        toasts = toasts.filter((t) => t.id !== id);
    }

    async function fetchCameraList() {
        try {
            const resp = await fetch(`${serverUrl}${apiEndpoint}`);
            if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
            sources = await resp.json();
            loading = false;
        } catch (err) {
            addToast(err instanceof Error ? err.message : "Unknown error");
            loading = false;
        }
    }

    async function refreshCameraList() {
        // show loading state while refreshing
        loading = true;
        await fetchCameraList();
    }

    async function fetchCameraDetails(key: SourceKey) {
        try {
            const resp = await fetch(
                `${serverUrl}${apiEndpoint}/source?camera_type=${encodeURIComponent(key.camera_type)}&camera_name=${encodeURIComponent(key.camera_name)}`,
                {
                    method: "GET",
                    headers: { "Content-Type": "application/json" },
                }
            );
            if (!resp.ok) {
                if (resp.status === 404) {
                    details = null;
                    addToast("Camera not found", "error");
                    return;
                }
                throw new Error(`HTTP ${resp.status}`);
            }
            details = await resp.json();
            // Ensure resolution selection defaults to an available resolution if provided
            if (
                details &&
                Array.isArray(details.resolutions_available) &&
                details.resolutions_available.length > 0
            ) {
                if (
                    !details.resolution ||
                    !details.resolutions_available.includes(details.resolution)
                ) {
                    details.resolution = details.resolutions_available[0];
                }
            }
        } catch (err) {
            addToast(err instanceof Error ? err.message : "Unknown error");
        }
    }

    async function saveCamera() {
        if (!selected || !details) return;
        saving = true;
        try {
            const body = {
                camera_type: selected.camera_type,
                camera_name: selected.camera_name,
                source_enabled: details?.source_enabled ?? false,
                frame_rate: details?.frame_rate ?? 30,
                resolution: details?.resolution ?? "1280x960",
                calibration_file: details?.calibration_file ?? "",
            };
            const resp = await fetch(
                `${serverUrl}${apiEndpoint}/source`,
                {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify(body),
                },
            );
            if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
            addToast("Saved", "success");
        } catch (err) {
            addToast(err instanceof Error ? err.message : "Unknown error");
        } finally {
            saving = false;
        }
    }

    onMount(() => {
        fetchCameraList();
    });
</script>

<div class="camera-source {className}">
    <div class="toast-container" aria-live="polite" aria-atomic="true">
        {#each toasts as toast (toast.id)}
            <div class="toast {toast.type}">
                {toast.message}
                <button
                    class="btn btn-secondary"
                    on:click={() => removeToast(toast.id)}>×</button
                >
            </div>
        {/each}
    </div>

    <h2>Camera Sources</h2>

    {#if loading}
        <div class="loading">Loading cameras...</div>
    {:else if sources.length === 0}
        <div class="empty-state">No cameras found</div>
    {:else}
        <div class="camera-layout">
            <div class="camera-list">
                {#each sources as source}
                    <div class="camera-item">
                        <button
                            class:active={sourceKeysEqual(selected, source)}
                            on:click={() => {
                                selected = source;
                                fetchCameraDetails(source);
                            }}>{source.camera_name} <span class="camera-type">({source.camera_type})</span></button
                        >
                    </div>
                {/each}
                <div class="camera-item refresh-item">
                    <button
                        class="refresh"
                        on:click={refreshCameraList}
                        disabled={loading}
                        aria-label="Refresh camera list"
                        title="Refresh camera list"
                    >
                        ⟳ Refresh
                    </button>
                </div>
            </div>

            <div class="camera-detail">
                {#if selected && details}
                    <h4>Camera: {selected.camera_name}</h4>
                    <table class="source-table">
                        <tbody>
                            <tr>
                                <td class="field">Type</td>
                                <td class="value">{selected.camera_type}</td>
                            </tr>
                            <tr>
                                <td class="field">Connected</td>
                                <td class="value">
                                    <span class:connected={details.source_connected}>
                                        {details.source_connected ? "Yes" : "No"}
                                    </span>
                                </td>
                            </tr>
                            <tr>
                                <td class="field">Enabled</td>
                                <td class="value">
                                    <input
                                        id={sanitizeForId(sourceKeyToString(selected)) +
                                            "-enabled"}
                                        type="checkbox"
                                        bind:checked={details.source_enabled}
                                    />
                                </td>
                            </tr>
                            <tr>
                                <td class="field">Frame Rate</td>
                                <td class="value">
                                    <input
                                        id={sanitizeForId(sourceKeyToString(selected)) +
                                            "-frame_rate"}
                                        type="number"
                                        bind:value={details.frame_rate}
                                        min="1"
                                    />
                                </td>
                            </tr>
                            <tr>
                                <td class="field">Resolution</td>
                                <td class="value">
                                    {#if details.resolutions_available && details.resolutions_available.length > 0}
                                        <select
                                            id={sanitizeForId(sourceKeyToString(selected)) +
                                                "-resolution"}
                                            bind:value={details.resolution}
                                        >
                                            {#each details.resolutions_available as res}
                                                <option value={res}
                                                    >{res}</option
                                                >
                                            {/each}
                                        </select>
                                    {:else}
                                        <input
                                            id={sanitizeForId(sourceKeyToString(selected)) +
                                                "-resolution"}
                                            type="text"
                                            bind:value={details.resolution}
                                        />
                                    {/if}
                                </td>
                            </tr>
                            <tr>
                                <td class="field">Calibration File</td>
                                <td class="value">
                                    {#if details.calibration_files_available && details.calibration_files_available.length > 0}
                                        <select
                                            id={sanitizeForId(sourceKeyToString(selected)) +
                                                "-calibration"}
                                            bind:value={details.calibration_file}
                                        >
                                            <option value="">None</option>
                                            {#each details.calibration_files_available as cal}
                                                <option value={cal}
                                                    >{cal}</option
                                                >
                                            {/each}
                                        </select>
                                    {:else}
                                        <input
                                            id={sanitizeForId(sourceKeyToString(selected)) +
                                                "-calibration"}
                                            type="text"
                                            bind:value={details.calibration_file}
                                        />
                                    {/if}
                                </td>
                            </tr>
                        </tbody>
                    </table>
                    <div class="actions">
                        <button
                            class="primary"
                            on:click={saveCamera}
                            disabled={saving}>Save</button
                        >
                        <button
                            class="secondary"
                            on:click={() => {
                                if (selected) fetchCameraDetails(selected);
                            }}
                            disabled={saving}>Reload</button
                        >
                    </div>
                {:else if selected}
                    <div class="loading">Loading details...</div>
                {:else}
                    <div class="empty-state">
                        Select a camera to view details
                    </div>
                {/if}
            </div>
        </div>
    {/if}
</div>

<style>
    .camera-source {
        padding: 12px;
        display: flex;
        flex-direction: column;
        height: 100%;
        box-sizing: border-box;
        color: #f9fafb;
    }
    .camera-layout {
        display: flex;
        gap: 12px;
        flex: 1 1 auto;
        overflow: hidden;
    }
    .camera-list {
        flex: 0 0 220px;
        display: flex;
        flex-direction: column;
        gap: 6px;
        height: 100%;
        overflow-y: auto;
    }
    .camera-item button {
        width: 100%;
        text-align: left;
        padding: 8px;
        border: 0;
        background: transparent;
        color: #d1d5db;
        border-radius: 6px;
        cursor: pointer;
    }
    .camera-item button:hover {
        background: #2a2a2a;
        color: #f9fafb;
    }
    .camera-item.refresh-item {
        margin-top: 6px;
    }
    .refresh {
        width: 100%;
        text-align: center;
        padding: 8px;
        border: 1px solid #333;
        border-radius: 6px;
        background: #1a1a1a;
        color: #d1d5db;
        cursor: pointer;
    }
    .refresh[disabled] {
        opacity: 0.6;
        cursor: default;
    }
    .camera-item button.active {
        background: #dc2626;
        color: white;
    }
    .camera-type {
        font-size: 0.85em;
        opacity: 0.7;
    }
    .camera-detail {
        flex: 1 1 auto;
        background: #222222;
        border: 1px solid #2a2a2a;
        padding: 12px;
        border-radius: 8px;
        height: 100%;
        overflow-y: auto;
        color: #f9fafb;
    }
    .source-table {
        width: 100%;
        border-collapse: collapse;
    }
    .source-table td.field {
        font-weight: 600;
        padding: 8px 12px;
        vertical-align: middle;
        width: 220px;
        color: #e5e7eb;
    }
    .source-table td.value {
        padding: 8px 12px;
    }
    .source-table input[type="number"],
    .source-table input[type="text"],
    .source-table select {
        padding: 6px 8px;
        border: 1px solid #333;
        border-radius: 4px;
        background: #1a1a1a;
        color: #f9fafb;
        font-size: 0.95rem;
    }
    .source-table input[type="checkbox"] {
        accent-color: #dc2626;
        width: 18px;
        height: 18px;
    }
    .actions {
        margin-top: 12px;
        display: flex;
        gap: 8px;
    }
    .primary {
        background: #dc2626;
        color: white;
        padding: 8px 12px;
        border-radius: 6px;
        border: none;
        cursor: pointer;
        font-weight: 600;
    }
    .primary:hover:not(:disabled) {
        background: #b91c1c;
    }
    .primary:disabled {
        opacity: 0.6;
        cursor: not-allowed;
    }
    .secondary {
        background: #1a1a1a;
        border: 1px solid #333;
        color: #d1d5db;
        padding: 8px 12px;
        border-radius: 6px;
        cursor: pointer;
    }
    .secondary:hover:not(:disabled) {
        background: #2a2a2a;
    }
    .secondary:disabled {
        opacity: 0.6;
        cursor: not-allowed;
    }
    .toast {
        padding: 8px 12px;
        border-radius: 6px;
        background: #dc2626;
        color: white;
        margin-bottom: 8px;
    }
    .toast.success {
        background: #059669;
    }
    .connected {
        color: #34d399;
        font-weight: 600;
    }
    h2 {
        margin: 0 0 12px 0;
        font-size: 1.5rem;
        font-weight: 600;
        color: #f9fafb;
    }
    h4 {
        margin: 0 0 12px 0;
        font-size: 1.25rem;
        font-weight: 600;
        color: #f9fafb;
    }
    .loading,
    .empty-state {
        padding: 24px;
        text-align: center;
        color: #9ca3af;
    }
    .toast-container {
        position: fixed;
        top: 16px;
        right: 16px;
        z-index: 1000;
    }
</style>
