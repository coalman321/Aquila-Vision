<script lang="ts">
    import { onMount } from "svelte";
    import { createEventDispatcher } from "svelte";

    const dispatch = createEventDispatcher();

    export let serverUrl: string = "http://localhost:8080";
    export let apiEndpoint: string = "/api/field_map";
    export let className: string = "";

    interface Pose3d {
        x: number;
        y: number;
        z: number;
        roll: number;
        pitch: number;
        yaw: number;
    }

    interface FieldPosition {
        id: string;
        pose: Pose3d;
        description: string;
    }

    let positions: FieldPosition[] = [];
    let loading = true;
    let saving = false;
    let toasts: Array<{ id: number; message: string; type?: string }> = [];
    let _nextToastId = 1;
    let fileInput: HTMLInputElement;

    // Form state for editing/creating
    let editMode = false;
    let formId = "";
    let formOriginalId = ""; // Track original ID for rename detection
    let formDescription = "";
    let formPose: Pose3d = { x: 0, y: 0, z: 0, roll: 0, pitch: 0, yaw: 0 };

    const defaultPose: Pose3d = { x: 0, y: 0, z: 0, roll: 0, pitch: 0, yaw: 0 };

    function addToast(message: string, type = "info") {
        const id = _nextToastId++;
        toasts = [...toasts, { id, message, type }];
        setTimeout(() => removeToast(id), 4000);
        return id;
    }

    function removeToast(id: number) {
        toasts = toasts.filter((t) => t.id !== id);
    }

    // Convert quaternion to Euler angles (roll, pitch, yaw)
    function quaternionToEuler(w: number, x: number, y: number, z: number): { roll: number; pitch: number; yaw: number } {
        // Roll (x-axis rotation)
        const sinr_cosp = 2 * (w * x + y * z);
        const cosr_cosp = 1 - 2 * (x * x + y * y);
        const roll = Math.atan2(sinr_cosp, cosr_cosp);

        // Pitch (y-axis rotation)
        const sinp = 2 * (w * y - z * x);
        const pitch = Math.abs(sinp) >= 1
            ? Math.sign(sinp) * Math.PI / 2 // use 90 degrees if out of range
            : Math.asin(sinp);

        // Yaw (z-axis rotation)
        const siny_cosp = 2 * (w * z + x * y);
        const cosy_cosp = 1 - 2 * (y * y + z * z);
        const yaw = Math.atan2(siny_cosp, cosy_cosp);

        return { roll, pitch, yaw };
    }

    async function fetchPositions() {
        loading = true;
        try {
            const resp = await fetch(`${serverUrl}${apiEndpoint}/positions`);
            if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
            positions = await resp.json();
            dispatch("positionsLoaded", { positions });
        } catch (err) {
            addToast(`Failed to load positions: ${err}`, "error");
            console.error("Error fetching positions:", err);
        } finally {
            loading = false;
        }
    }

    async function savePosition() {
        if (!formId.trim()) {
            addToast("Position ID is required", "error");
            return;
        }

        saving = true;
        try {
            const payload: any = {
                id: formId,
                description: formDescription,
                pose: { ...formPose }
            };

            // If editing and ID changed, include old_id for rename
            if (formOriginalId && formOriginalId !== formId) {
                payload.old_id = formOriginalId;
            }

            const resp = await fetch(`${serverUrl}${apiEndpoint}/position`, {
                method: "POST",
                headers: { "Content-Type": "application/json" },
                body: JSON.stringify(payload)
            });

            if (!resp.ok) {
                const errData = await resp.json().catch(() => ({}));
                throw new Error(errData.error || `HTTP ${resp.status}`);
            }

            const result = await resp.json();
            addToast(result.message || "Position saved successfully", "success");
            dispatch("positionSaved", { position: payload });
            
            await fetchPositions();
            cancelEdit();
        } catch (err) {
            addToast(`Failed to save position: ${err}`, "error");
            console.error("Error saving position:", err);
        } finally {
            saving = false;
        }
    }

    async function deletePosition(id: string) {
        if (!confirm(`Are you sure you want to delete position "${id}"?`)) {
            return;
        }

        saving = true;
        try {
            const resp = await fetch(`${serverUrl}${apiEndpoint}/position?id=${encodeURIComponent(id)}`, {
                method: "DELETE"
            });

            if (!resp.ok) {
                const errData = await resp.json().catch(() => ({}));
                throw new Error(errData.error || `HTTP ${resp.status}`);
            }

            const result = await resp.json();
            addToast(result.message || "Position deleted successfully", "success");
            dispatch("positionDeleted", { id });
            
            await fetchPositions();
        } catch (err) {
            addToast(`Failed to delete position: ${err}`, "error");
            console.error("Error deleting position:", err);
        } finally {
            saving = false;
        }
    }

    function startNewPosition() {
        editMode = true;
        formId = "";
        formOriginalId = "";
        formDescription = "";
        formPose = { ...defaultPose };
    }

    function editPosition(position: FieldPosition) {
        editMode = true;
        formId = position.id;
        formOriginalId = position.id;
        formDescription = position.description;
        formPose = { ...position.pose };
    }

    function cancelEdit() {
        editMode = false;
        formId = "";
        formOriginalId = "";
        formDescription = "";
        formPose = { ...defaultPose };
    }

    async function clearAllPositions() {
        if (!confirm(`Are you sure you want to delete ALL ${positions.length} position(s)? This cannot be undone.`)) {
            return;
        }

        saving = true;
        const deletePromises = positions.map(pos => 
            fetch(`${serverUrl}${apiEndpoint}/position?id=${encodeURIComponent(pos.id)}`, {
                method: "DELETE"
            })
        );

        try {
            const results = await Promise.all(deletePromises);
            const failed = results.filter(r => !r.ok);
            
            if (failed.length > 0) {
                throw new Error(`Failed to delete ${failed.length} position(s)`);
            }

            addToast(`Successfully deleted all ${positions.length} positions`, "success");
            dispatch("allPositionsCleared");
            await fetchPositions();
        } catch (err) {
            addToast(`Failed to clear all positions: ${err}`, "error");
            console.error("Error clearing positions:", err);
        } finally {
            saving = false;
        }
    }

    function triggerFileInput() {
        fileInput?.click();
    }

    async function handleFileUpload(event: Event) {
        const target = event.target as HTMLInputElement;
        const file = target.files?.[0];
        
        if (!file) return;

        try {
            const content = await file.text();
            const data = JSON.parse(content);

            if (!data.tags || !Array.isArray(data.tags)) {
                throw new Error("Invalid JSON format: missing 'tags' array");
            }

            const newPositions: FieldPosition[] = data.tags.map((tag: any) => {
                const id = tag.ID;
                const translation = tag.pose?.translation || {};
                const quaternion = tag.pose?.rotation?.quaternion || {};

                // Convert quaternion to Euler angles
                const { roll, pitch, yaw } = quaternionToEuler(
                    quaternion.W || 0,
                    quaternion.X || 0,
                    quaternion.Y || 0,
                    quaternion.Z || 0
                );

                return {
                    id: `tag_${id}`,
                    description: `AprilTag ${id}`,
                    pose: {
                        x: translation.x || 0,
                        y: translation.y || 0,
                        z: translation.z || 0,
                        roll,
                        pitch,
                        yaw
                    }
                };
            });

            // Save all positions to server
            saving = true;
            const savePromises = newPositions.map(pos =>
                fetch(`${serverUrl}${apiEndpoint}/position`, {
                    method: "POST",
                    headers: { "Content-Type": "application/json" },
                    body: JSON.stringify(pos)
                })
            );

            const results = await Promise.all(savePromises);
            const failed = results.filter(r => !r.ok);

            if (failed.length > 0) {
                throw new Error(`Failed to save ${failed.length} position(s)`);
            }

            addToast(`Successfully loaded ${newPositions.length} positions from JSON`, "success");
            dispatch("positionsLoadedFromJson", { positions: newPositions });
            await fetchPositions();
        } catch (err) {
            addToast(`Failed to load JSON: ${err}`, "error");
            console.error("Error loading JSON:", err);
        } finally {
            saving = false;
            // Reset file input
            target.value = "";
        }
    }

    onMount(() => {
        fetchPositions();
    });
</script>

<div class={`field-map-manager ${className}`}>
    <!-- Toast notifications -->
    {#if toasts.length > 0}
        <div class="toast-container">
            {#each toasts as toast (toast.id)}
                <div class="toast {toast.type || 'info'}">
                    <span>{toast.message}</span>
                    <button
                        class="toast-close"
                        on:click={() => removeToast(toast.id)}
                        aria-label="Close notification"
                    >
                        ×
                    </button>
                </div>
            {/each}
        </div>
    {/if}

    <div class="manager-layout">
        {#if editMode}
            <div class="edit-panel">
                <div class="edit-form">
                    <h3>{formId && positions.some(p => p.id === formId) ? 'Edit' : 'New'} Position</h3>
                    
                    <div class="form-group">
                        <label for="position-id">
                            ID <span class="required">*</span>
                        </label>
                        <input
                            id="position-id"
                            type="text"
                            bind:value={formId}
                            placeholder="unique_position_id"
                            required
                        />
                    </div>

                    <div class="form-group">
                        <label for="position-description">Description</label>
                        <textarea
                            id="position-description"
                            bind:value={formDescription}
                            placeholder="Optional description"
                            rows="2"
                        ></textarea>
                    </div>

                    <fieldset class="pose-fieldset">
                        <legend>Pose (3D Position & Orientation)</legend>
                        
                        <div class="pose-grid">
                            <div class="form-group">
                                <label for="pose-x">X (m)</label>
                                <input
                                    id="pose-x"
                                    type="number"
                                    step="0.01"
                                    bind:value={formPose.x}
                                />
                            </div>
                            <div class="form-group">
                                <label for="pose-y">Y (m)</label>
                                <input
                                    id="pose-y"
                                    type="number"
                                    step="0.01"
                                    bind:value={formPose.y}
                                />
                            </div>
                            <div class="form-group">
                                <label for="pose-z">Z (m)</label>
                                <input
                                    id="pose-z"
                                    type="number"
                                    step="0.01"
                                    bind:value={formPose.z}
                                />
                            </div>
                            <div class="form-group">
                                <label for="pose-roll">Roll (rad)</label>
                                <input
                                    id="pose-roll"
                                    type="number"
                                    step="0.01"
                                    bind:value={formPose.roll}
                                />
                            </div>
                            <div class="form-group">
                                <label for="pose-pitch">Pitch (rad)</label>
                                <input
                                    id="pose-pitch"
                                    type="number"
                                    step="0.01"
                                    bind:value={formPose.pitch}
                                />
                            </div>
                            <div class="form-group">
                                <label for="pose-yaw">Yaw (rad)</label>
                                <input
                                    id="pose-yaw"
                                    type="number"
                                    step="0.01"
                                    bind:value={formPose.yaw}
                                />
                            </div>
                        </div>
                    </fieldset>

                    <div class="form-actions">
                        <button
                            class="btn-primary"
                            on:click={savePosition}
                            disabled={saving || !formId.trim()}
                        >
                            {saving ? 'Saving...' : 'Save Position'}
                        </button>
                        <button
                            class="btn-secondary"
                            on:click={cancelEdit}
                            disabled={saving}
                        >
                            Cancel
                        </button>
                    </div>
                </div>
            </div>
        {:else}
            <div class="positions-panel">
                <div class="panel-header">
                    <h3>Field Positions</h3>
                    <div class="header-actions">
                        <button
                            class="btn-secondary"
                            on:click={triggerFileInput}
                            disabled={loading || saving}
                            title="Load positions from JSON file"
                        >
                            📁 Load JSON
                        </button>
                        <button
                            class="btn-danger"
                            on:click={clearAllPositions}
                            disabled={loading || saving || positions.length === 0}
                            title="Delete all positions"
                        >
                            🗑 Clear All
                        </button>
                        <button
                            class="btn-primary"
                            on:click={startNewPosition}
                            disabled={loading || saving}
                        >
                            + New Position
                        </button>
                    </div>
                    <input
                        type="file"
                        accept=".json"
                        bind:this={fileInput}
                        on:change={handleFileUpload}
                        style="display: none;"
                    />
                </div>

                {#if loading}
                    <div class="loading">Loading positions...</div>
                {:else if positions.length === 0}
                    <div class="empty-state">
                        No positions configured. Click "New Position" to add one.
                    </div>
                {:else}
                    <div class="table-container">
                        <table class="positions-table">
                            <thead>
                                <tr>
                                    <th>ID</th>
                                    <th>Description</th>
                                    <th>X (m)</th>
                                    <th>Y (m)</th>
                                    <th>Z (m)</th>
                                    <th>Roll (rad)</th>
                                    <th>Pitch (rad)</th>
                                    <th>Yaw (rad)</th>
                                    <th>Actions</th>
                                </tr>
                            </thead>
                            <tbody>
                                {#each positions as position (position.id)}
                                    <tr class:editing={editMode && formId === position.id}>
                                        <td class="cell-id">{position.id}</td>
                                        <td class="cell-description">{position.description || '—'}</td>
                                        <td class="cell-value">{position.pose.x.toFixed(3)}</td>
                                        <td class="cell-value">{position.pose.y.toFixed(3)}</td>
                                        <td class="cell-value">{position.pose.z.toFixed(3)}</td>
                                        <td class="cell-value">{position.pose.roll.toFixed(3)}</td>
                                        <td class="cell-value">{position.pose.pitch.toFixed(3)}</td>
                                        <td class="cell-value">{position.pose.yaw.toFixed(3)}</td>
                                        <td class="cell-actions">
                                            <button
                                                class="btn-icon"
                                                on:click={() => editPosition(position)}
                                                disabled={saving}
                                                title="Edit"
                                            >
                                                ✎
                                            </button>
                                            <button
                                                class="btn-icon btn-danger"
                                                on:click={() => deletePosition(position.id)}
                                                disabled={saving}
                                                title="Delete"
                                            >
                                                🗑
                                            </button>
                                        </td>
                                    </tr>
                                {/each}
                            </tbody>
                        </table>
                    </div>
                {/if}
            </div>
        {/if}
    </div>
</div>

<style>
    .field-map-manager {
        display: flex;
        flex-direction: column;
        height: 100%;
        width: 100%;
        max-width: 100%;
        box-sizing: border-box;
        overflow: hidden;
        color: #f9fafb;
    }

    .toast-container {
        position: fixed;
        top: 20px;
        right: 20px;
        z-index: 1000;
        display: flex;
        flex-direction: column;
        gap: 8px;
        max-width: 400px;
    }

    .toast {
        padding: 12px 16px;
        border-radius: 6px;
        background: #1a1a1a;
        box-shadow: 0 4px 12px rgba(0, 0, 0, 0.5);
        display: flex;
        justify-content: space-between;
        align-items: center;
        gap: 12px;
        animation: slideIn 0.3s ease-out;
        color: #f9fafb;
    }

    @keyframes slideIn {
        from {
            transform: translateX(100%);
            opacity: 0;
        }
        to {
            transform: translateX(0);
            opacity: 1;
        }
    }

    .toast.success {
        border-left: 4px solid #10b981;
    }

    .toast.error {
        border-left: 4px solid #dc2626;
    }

    .toast.info {
        border-left: 4px solid #3b82f6;
    }

    .toast-close {
        background: none;
        border: none;
        font-size: 20px;
        cursor: pointer;
        color: #9ca3af;
        padding: 0;
        width: 24px;
        height: 24px;
        display: flex;
        align-items: center;
        justify-content: center;
    }

    .toast-close:hover {
        color: #f9fafb;
    }

    .manager-layout {
        display: flex;
        flex-direction: column;
        height: 100%;
        overflow: hidden;
        padding: 0 16px;
        box-sizing: border-box;
    }

    .edit-panel,
    .positions-panel {
        flex: 1;
        overflow-y: auto;
        display: flex;
        flex-direction: column;
        min-width: 0;
        max-width: 100%;
    }

    .panel-header {
        display: flex;
        justify-content: space-between;
        align-items: center;
        margin-bottom: 20px;
        flex-shrink: 0;
        flex-wrap: wrap;
        gap: 12px;
        max-width: 100%;
    }

    .panel-header h3 {
        margin: 0;
        font-size: 1.5rem;
        color: #f9fafb;
    }

    .header-actions {
        display: flex;
        gap: 8px;
        flex-wrap: wrap;
        flex-shrink: 0;
    }

    .table-container {
        flex: 1;
        overflow: auto;
        border: 1px solid #2a2a2a;
        border-radius: 8px;
        background: #1a1a1a;
        max-width: 100%;
    }

    .positions-table {
        width: 100%;
        border-collapse: collapse;
        font-size: 0.875rem;
    }

    .positions-table thead {
        background: #2a2a2a;
        position: sticky;
        top: 0;
        z-index: 10;
    }

    .positions-table th {
        padding: 12px 16px;
        text-align: left;
        font-weight: 600;
        color: #e5e7eb;
        border-bottom: 2px solid #333;
        white-space: nowrap;
    }

    .positions-table th:nth-child(n+3):nth-child(-n+8) {
        text-align: center;
    }

    .positions-table td {
        padding: 12px 16px;
        border-bottom: 1px solid #222;
    }

    .positions-table tbody tr:last-child td {
        border-bottom: none;
    }

    .positions-table tbody tr:hover {
        background: #222;
    }

    .positions-table tbody tr.editing {
        background: #1e2a3a;
    }

    .cell-id {
        font-weight: 500;
        color: #f9fafb;
    }

    .cell-description {
        color: #9ca3af;
        max-width: 200px;
        overflow: hidden;
        text-overflow: ellipsis;
        white-space: nowrap;
    }

    .cell-value {
        font-family: monospace;
        color: #f9fafb;
        text-align: center;
    }

    .cell-actions {
        display: flex;
        gap: 4px;
        justify-content: flex-end;
    }

    .loading,
    .empty-state {
        padding: 48px 24px;
        text-align: center;
        color: #9ca3af;
    }

    .edit-form {
        max-width: 800px;
    }

    .edit-form h3 {
        margin-top: 0;
        margin-bottom: 24px;
        color: #f9fafb;
        font-size: 1.5rem;
    }

    .form-group {
        margin-bottom: 16px;
    }

    .form-group label {
        display: block;
        margin-bottom: 6px;
        color: #e5e7eb;
        font-weight: 500;
        font-size: 0.875rem;
    }

    .required {
        color: #f87171;
    }

    .form-group input,
    .form-group textarea {
        width: 100%;
        padding: 8px 12px;
        border: 1px solid #333;
        border-radius: 6px;
        font-size: 0.875rem;
        font-family: inherit;
        box-sizing: border-box;
        background: #1a1a1a;
        color: #f9fafb;
    }

    .form-group input:focus,
    .form-group textarea:focus {
        outline: none;
        border-color: #dc2626;
        box-shadow: 0 0 0 3px rgba(220, 38, 38, 0.2);
    }

    .form-group input:disabled {
        background: #2a2a2a;
        cursor: not-allowed;
        color: #9ca3af;
    }

    .help-text {
        display: block;
        margin-top: 4px;
        font-size: 0.75rem;
        color: #9ca3af;
    }

    .pose-fieldset {
        border: 1px solid #333;
        border-radius: 6px;
        padding: 16px;
        margin-bottom: 20px;
    }

    .pose-fieldset legend {
        padding: 0 8px;
        color: #e5e7eb;
        font-weight: 500;
        font-size: 0.875rem;
    }

    .pose-grid {
        display: grid;
        grid-template-columns: repeat(3, 1fr);
        gap: 12px;
    }

    .form-actions {
        display: flex;
        gap: 12px;
        margin-top: 24px;
    }

    .btn-primary,
    .btn-secondary,
    .btn-danger,
    .btn-icon {
        padding: 10px 16px;
        border-radius: 6px;
        font-size: 0.875rem;
        font-weight: 500;
        cursor: pointer;
        transition: all 0.2s;
        border: none;
        white-space: nowrap;
        flex-shrink: 0;
    }

    .btn-primary {
        background: #dc2626;
        color: white;
    }

    .btn-primary:hover:not(:disabled) {
        background: #b91c1c;
    }

    .btn-primary:disabled {
        background: #4b4b4b;
        cursor: not-allowed;
    }

    .btn-secondary {
        background: #2a2a2a;
        color: #d1d5db;
        border: 1px solid #333;
    }

    .btn-secondary:hover:not(:disabled) {
        background: #333;
    }

    .btn-secondary:disabled {
        opacity: 0.5;
        cursor: not-allowed;
    }

    .btn-danger {
        background: #ef4444;
        color: white;
    }

    .btn-danger:hover:not(:disabled) {
        background: #dc2626;
    }

    .btn-danger:disabled {
        background: #4b4b4b;
        cursor: not-allowed;
    }

    .btn-icon {
        padding: 6px 10px;
        background: #2a2a2a;
        color: #d1d5db;
        font-size: 1rem;
        border: 1px solid #333;
    }

    .btn-icon:hover:not(:disabled) {
        background: #333;
    }

    .btn-icon.btn-danger {
        background: #3b0a0a;
        color: #f87171;
        border-color: #7f1d1d;
    }

    .btn-icon.btn-danger:hover:not(:disabled) {
        background: #7f1d1d;
        color: white;
    }

    @media (max-width: 768px) {
        .pose-grid {
            grid-template-columns: repeat(2, 1fr);
        }

        .positions-table {
            font-size: 0.75rem;
        }

        .positions-table th,
        .positions-table td {
            padding: 8px 12px;
        }

        .cell-description {
            max-width: 120px;
        }
    }
</style>
