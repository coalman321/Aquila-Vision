<script lang="ts">
  import { createEventDispatcher } from "svelte";
  import { onMount } from "svelte";
  import { slide } from "svelte/transition";
  import { CameraViewer } from "$framework";

  const dispatch = createEventDispatcher();

  export let serverUrl: string = "http://localhost:8080";
  export let className: string = "";

  type DetectionType = "apriltag" | "object_detection";

  type Pose = {
    x: number;
    y: number;
    z: number;
    roll: number;
    pitch: number;
    yaw: number;
  };

  const defaultPose: Pose = { x: 0, y: 0, z: 0, roll: 0, pitch: 0, yaw: 0 };
  
  // State
  let pipelines: string[] = [];
  let selectedPipeline: string | null = null;
  let loading = true;
  let saving = false;
  let toasts: Array<{ id: number; message: string; type?: string }> = [];
  let _nextToastId = 1;

  // Pipeline configuration
  let pose: Pose = { ...defaultPose };
  let gain = 1.0;
  let exposure = 1.0;
  let enabledDetections: DetectionType[] = [];
  let apriltagFamily = "tag36h11";
  let apriltagSize = 0.162;
  let objectDetectionModel = "yolov8n";
  let objectDetectionConfidence = 0.5;
  let availableModels: string[] = [];
  let apriltagFamilies: string[] = [];

  // Accordion state
  let detectionOpen: Record<DetectionType, boolean> = {
    apriltag: false,
    object_detection: false,
  };

  $: modelValid =
    !enabledDetections.includes("object_detection") ||
    availableModels.includes(objectDetectionModel);

  function addToast(message: string, type = "error") {
    const id = _nextToastId++;
    toasts = [...toasts, { id, message, type }];
    setTimeout(() => removeToast(id), 4000);
    return id;
  }

  function removeToast(id: number) {
    toasts = toasts.filter((t) => t.id !== id);
  }

  async function fetchPipelines() {
    try {
      const base = serverUrl.replace(/\/$/, "");
      const url = `${base}/api/pipelines`;
      const rsp = await fetch(url);
      if (!rsp.ok) {
        pipelines = [];
        return;
      }
      const j = await rsp.json();
      if (j.pipelines && Array.isArray(j.pipelines) && j.pipelines.length) {
        pipelines = j.pipelines.map((p: any) => {
          if (typeof p === 'string') {
            return p;
          } else if (p.camera_type && p.camera_name) {
            const prefix = p.camera_type.toLowerCase();
            return `${prefix}_${p.camera_name}`;
          } else {
            return p.camera_name || p.toString();
          }
        });
      } else {
        pipelines = [];
      }
      loading = false;
    } catch (e) {
      addToast(e instanceof Error ? e.message : "Unknown error");
      pipelines = [];
      loading = false;
    }
  }

  async function refreshPipelines() {
    loading = true;
    await fetchPipelines();
  }

  async function fetchPipelineConfig(pipelineName: string) {
    loading = true;
    try {
      const base = serverUrl.replace(/\/$/, "");
      const url = `${base}/api/pipelines/${encodeURIComponent(pipelineName)}`;
      const rsp = await fetch(url);
      if (!rsp.ok) {
        throw new Error(`HTTP ${rsp.status}: ${rsp.statusText}`);
      }
      const j = await rsp.json();
      
      // Parse camera_pose
      if (j.camera_pose) {
        const cp = j.camera_pose;
        pose = {
          x: cp.x ?? 0,
          y: cp.y ?? 0,
          z: cp.z ?? 0,
          roll: cp.roll ?? 0,
          pitch: cp.pitch ?? 0,
          yaw: cp.yaw ?? 0,
        };
      } else {
        pose = { ...defaultPose };
      }
      gain = j.gain ?? 1.0;
      exposure = j.exposure ?? 1.0;

      // Parse detection_config
      enabledDetections = [];
      if (j.detection_config) {
        if (j.detection_config.enabled) {
          enabledDetections.push("object_detection");
        }
        objectDetectionModel = j.detection_config.model_name ?? objectDetectionModel;
        objectDetectionConfidence = j.detection_config.confidence_threshold ?? objectDetectionConfidence;
        
        // Parse available models from detection_config
        if (j.detection_config.models && Array.isArray(j.detection_config.models)) {
          availableModels = j.detection_config.models;
        } else {
          availableModels = [];
        }
      }

      // Parse apriltag_config
      if (j.apriltag_config) {
        if (j.apriltag_config.enabled) {
          enabledDetections.push("apriltag");
        }
        apriltagFamily = j.apriltag_config.family ? 
          j.apriltag_config.family.toLowerCase() : apriltagFamily;
        apriltagSize = j.apriltag_config.tag_size ?? apriltagSize;
        
        // Parse available apriltag families from apriltag_config
        if (j.apriltag_config.families && Array.isArray(j.apriltag_config.families)) {
          apriltagFamilies = j.apriltag_config.families.map((f: string) => f.toLowerCase());
        } else {
          apriltagFamilies = [];
        }
      }

      // Set defaults only for apriltag families if empty
      if (apriltagFamilies.length === 0) {
        apriltagFamilies = ["tag36h11", "tag25h9", "tag16h5"];
      }
    } catch (e) {
      addToast(e instanceof Error ? e.message : "Unknown error");
    } finally {
      loading = false;
    }
  }

  function toggleAccordion(type: DetectionType) {
    detectionOpen[type] = !detectionOpen[type];
  }

  function toggleDetection(type: DetectionType, checked: boolean) {
    if (checked && !enabledDetections.includes(type)) {
      enabledDetections = [...enabledDetections, type];
    } else if (!checked && enabledDetections.includes(type)) {
      enabledDetections = enabledDetections.filter((t) => t !== type);
    }
    if (checked && type === "object_detection") {
      if (availableModels.length && !availableModels.includes(objectDetectionModel)) {
        objectDetectionModel = availableModels[0];
      }
    }
    if (checked && type === "apriltag") {
      if (apriltagFamilies.length && !apriltagFamilies.includes(apriltagFamily)) {
        apriltagFamily = apriltagFamilies[0];
      }
    }
  }

  async function savePipeline() {
    if (!selectedPipeline) return;
    if (!modelValid) {
      addToast(`Please select an available object detection model: ${availableModels.join(", ")}`);
      return;
    }
    saving = true;
    try {
      const serverPayload = {
        camera_pose: {
          x: pose.x,
          y: pose.y,
          z: pose.z,
          roll: pose.roll,
          pitch: pose.pitch,
          yaw: pose.yaw
        },
        gain: Number(gain),
        exposure: Number(exposure),
        detection_config: {
          enabled: enabledDetections.includes("object_detection"),
          model_name: objectDetectionModel,
          confidence_threshold: objectDetectionConfidence,
          iou_threshold: 0.45
        },
        apriltag_config: {
          enabled: enabledDetections.includes("apriltag"),
          family: apriltagFamily.toUpperCase(),
          tag_size: apriltagSize
        }
      };
      const url = `${serverUrl.replace(/\/$/, "")}/api/pipelines/${encodeURIComponent(selectedPipeline)}`;
      const res = await fetch(url, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(serverPayload),
      });
      if (!res.ok) {
        const txt = await res.text();
        throw new Error(`HTTP ${res.status}: ${txt}`);
      }
      addToast("Saved", "success");
    } catch (e) {
      addToast(e instanceof Error ? e.message : "Unknown error");
    } finally {
      saving = false;
    }
  }

  function resetToDefaults() {
    pose = { ...defaultPose };
    gain = 1.0;
    exposure = 1.0;
    enabledDetections = [];
    apriltagFamily = apriltagFamilies.length ? apriltagFamilies[0] : "tag36h11";
    apriltagSize = 0.162;
    objectDetectionModel = availableModels.length ? availableModels[0] : "yolov8n";
    objectDetectionConfidence = 0.5;
  }

  onMount(async () => {
    await fetchPipelines();
    // Auto-select first pipeline if available
    if (pipelines.length > 0) {
      selectedPipeline = pipelines[0];
      await fetchPipelineConfig(selectedPipeline);
    }
  });
</script>

<div class="pipeline-configurator {className}">
  <div class="toast-container" aria-live="polite" aria-atomic="true">
    {#each toasts as toast (toast.id)}
      <div class="toast {toast.type}">
        {toast.message}
        <button class="btn btn-secondary" on:click={() => removeToast(toast.id)}>×</button>
      </div>
    {/each}
  </div>

  <h2>Pipeline Configuration</h2>

  {#if loading}
    <div class="loading">Loading pipelines...</div>
  {:else if pipelines.length === 0}
    <div class="empty-state">No pipelines found</div>
  {:else}
    <div class="pipeline-layout">
      <div class="pipeline-list">
        {#each pipelines as pipeline}
          <div class="pipeline-item">
            <button
              class:active={selectedPipeline === pipeline}
              on:click={() => {
                selectedPipeline = pipeline;
                fetchPipelineConfig(pipeline);
              }}>{pipeline}</button>
          </div>
        {/each}
        <div class="pipeline-item refresh-item">
          <button
            class="refresh"
            on:click={refreshPipelines}
            disabled={loading}
            aria-label="Refresh pipeline list"
            title="Refresh pipeline list"
          >
            ⟳ Refresh
          </button>
        </div>
      </div>

      <div class="pipeline-detail">
        {#if selectedPipeline}
          <h4>Pipeline: {selectedPipeline}</h4>
          
          <div class="top-section">
            <div class="preview-section">
              <h5>Camera Preview</h5>
              <div class="camera-wrapper">
                <CameraViewer serverUrl={(() => {
                  const url = new URL(serverUrl);
                  url.port = String(parseInt(url.port || '80') + 1);
                  return url.toString().replace(/\/$/, '');
                })()} defaultStream={`/${selectedPipeline}/driver/image_raw`} />
              </div>
            </div>

            <div class="config-section">
              <h5>Camera Pose</h5>
              <table class="pose-table">
                <thead>
                  <tr>
                    <th>X (m)</th>
                    <th>Y (m)</th>
                    <th>Z (m)</th>
                  </tr>
                </thead>
                <tbody>
                  <tr>
                    <td><input type="number" step="0.001" bind:value={pose.x} /></td>
                    <td><input type="number" step="0.001" bind:value={pose.y} /></td>
                    <td><input type="number" step="0.001" bind:value={pose.z} /></td>
                  </tr>
                </tbody>
              </table>
              <table class="pose-table">
                <thead>
                  <tr>
                    <th>Roll (rad)</th>
                    <th>Pitch (rad)</th>
                    <th>Yaw (rad)</th>
                  </tr>
                </thead>
                <tbody>
                  <tr>
                    <td><input type="number" step="0.01" bind:value={pose.roll} /></td>
                    <td><input type="number" step="0.01" bind:value={pose.pitch} /></td>
                    <td><input type="number" step="0.01" bind:value={pose.yaw} /></td>
                  </tr>
                </tbody>
              </table>

              <h5 style="margin-top: 16px;">Camera Settings</h5>
              <table class="config-table">
                <tbody>
                  <tr>
                    <td class="field">Gain</td>
                    <td class="value">
                      <input type="range" min="0" max="1" step="0.1" bind:value={gain} />
                      <span class="range-value">{gain}</span>
                    </td>
                  </tr>
                  <tr>
                    <td class="field">Exposure</td>
                    <td class="value">
                      <input type="range" min="500" max="10000" step="10" bind:value={exposure} />
                      <span class="range-value">{exposure}</span>
                    </td>
                  </tr>
                </tbody>
              </table>
            </div>
          </div>

          <div class="detection-section">
            <h5>Detection Pipelines</h5>
            
            <div class="accordion-item">
              <div class="accordion-header">
                <input
                  type="checkbox"
                  id="apriltag-enabled"
                  checked={enabledDetections.includes("apriltag")}
                  on:change={(e: Event) => {
                    const checked = (e.currentTarget as HTMLInputElement).checked;
                    toggleDetection("apriltag", checked);
                    if (checked) detectionOpen.apriltag = true;
                  }}
                />
                <button
                  type="button"
                  class="accordion-button"
                  class:expanded={detectionOpen.apriltag}
                  on:click={() => toggleAccordion("apriltag")}
                >
                  <span class="accordion-title">Apriltags</span>
                  <span class="accordion-icon">{detectionOpen.apriltag ? '−' : '+'}</span>
                </button>
              </div>
              {#if detectionOpen.apriltag}
                <div class="accordion-content" transition:slide>
                  {#if enabledDetections.includes("apriltag")}
                    <table class="config-table">
                      <tbody>
                        <tr>
                          <td class="field">Tag Family</td>
                          <td class="value">
                            <select bind:value={apriltagFamily}>
                              {#if apriltagFamilies.length}
                                {#each apriltagFamilies as f}
                                  <option value={f}>{f}</option>
                                {/each}
                              {:else}
                                <option value="tag16h5">tag16h5</option>
                                <option value="tag25h9">tag25h9</option>
                                <option value="tag36h11">tag36h11</option>
                              {/if}
                            </select>
                          </td>
                        </tr>
                        <tr>
                          <td class="field">Tag Size (m)</td>
                          <td class="value">
                            <input 
                              type="number" 
                              step="0.0001" 
                              value={apriltagSize.toFixed(4)}
                              on:change={(e) => {
                                apriltagSize = parseFloat(e.currentTarget.value);
                              }}
                            />
                          </td>
                        </tr>
                      </tbody>
                    </table>
                  {:else}
                    <div class="disabled-message">Enable apriltags to configure settings</div>
                  {/if}
                </div>
              {/if}
            </div>

            <div class="accordion-item">
              <div class="accordion-header">
                <input
                  type="checkbox"
                  id="object-detection-enabled"
                  checked={enabledDetections.includes("object_detection")}
                  on:change={(e: Event) => {
                    const checked = (e.currentTarget as HTMLInputElement).checked;
                    toggleDetection("object_detection", checked);
                    if (checked) detectionOpen.object_detection = true;
                  }}
                />
                <button
                  type="button"
                  class="accordion-button"
                  class:expanded={detectionOpen.object_detection}
                  on:click={() => toggleAccordion("object_detection")}
                >
                  <span class="accordion-title">Object Detection</span>
                  <span class="accordion-icon">{detectionOpen.object_detection ? '−' : '+'}</span>
                </button>
              </div>
              {#if detectionOpen.object_detection}
                <div class="accordion-content" transition:slide>
                  {#if enabledDetections.includes("object_detection")}
                    <table class="config-table">
                      <tbody>
                        <tr>
                          <td class="field">Model</td>
                          <td class="value">
                            <select bind:value={objectDetectionModel}>
                              {#if availableModels.length > 0}
                                {#each availableModels as m}
                                  <option value={m}>{m}</option>
                                {/each}
                              {:else}
                                <option value="">No models found</option>
                              {/if}
                            </select>
                          </td>
                        </tr>
                        {#if !modelValid}
                          <tr>
                            <td colspan="2" class="warning-cell">
                              Selected model <strong>{objectDetectionModel}</strong> is not available.
                            </td>
                          </tr>
                        {/if}
                        <tr>
                          <td class="field">Confidence Threshold</td>
                          <td class="value">
                            <input
                              type="range"
                              min="0"
                              max="1"
                              step="0.01"
                              bind:value={objectDetectionConfidence}
                            />
                            <span class="range-value">{objectDetectionConfidence}</span>
                          </td>
                        </tr>
                      </tbody>
                    </table>
                  {:else}
                    <div class="disabled-message">Enable object detection to configure settings</div>
                  {/if}
                </div>
              {/if}
            </div>
          </div>

          <div class="actions">
            <button class="primary" on:click={savePipeline} disabled={saving || !modelValid}>Save</button>
            <button class="secondary" on:click={resetToDefaults} disabled={saving}>Reset</button>
            <button
              class="secondary"
              on:click={() => {
                if (selectedPipeline) fetchPipelineConfig(selectedPipeline);
              }}
              disabled={saving}
            >
              Reload
            </button>
          </div>
        {:else}
          <div class="empty-state">Select a pipeline to view configuration</div>
        {/if}
      </div>
    </div>
  {/if}
</div>

<style>
  .pipeline-configurator {
    padding: 12px;
    display: flex;
    flex-direction: column;
    width: 100%;
    box-sizing: border-box;
    color: #f9fafb;
  }

  .toast-container {
    position: fixed;
    top: 16px;
    right: 16px;
    z-index: 1000;
  }

  .toast {
    padding: 8px 12px;
    border-radius: 6px;
    background: #dc2626;
    color: white;
    margin-bottom: 8px;
    display: flex;
    align-items: center;
    gap: 8px;
  }

  .toast.success {
    background: #059669;
  }

  .toast button {
    background: transparent;
    border: none;
    color: white;
    font-size: 20px;
    cursor: pointer;
    padding: 0 4px;
  }

  h2 {
    margin: 0 0 12px 0;
    font-size: 1.5rem;
    font-weight: 600;
    color: #f9fafb;
  }

  .loading,
  .empty-state {
    padding: 24px;
    text-align: center;
    color: #9ca3af;
  }

  .pipeline-layout {
    display: flex;
    gap: 12px;
    flex: 1 1 auto;
  }

  .pipeline-list {
    flex: 0 0 220px;
    display: flex;
    flex-direction: column;
    gap: 6px;
  }

  .pipeline-item button {
    width: 100%;
    text-align: left;
    padding: 8px 12px;
    border: 0;
    background: transparent;
    cursor: pointer;
    border-radius: 6px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
    font-size: 0.95rem;
    color: #d1d5db;
  }

  .pipeline-item button:hover {
    background: #2a2a2a;
    color: #f9fafb;
  }

  .pipeline-item button.active {
    background: #dc2626;
    color: white;
  }

  .pipeline-item.refresh-item {
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

  .pipeline-detail {
    flex: 1 1 auto;
    background: #222222;
    border: 1px solid #2a2a2a;
    padding: 12px;
    border-radius: 8px;
    min-width: 0;
    color: #f9fafb;
  }

  h4 {
    margin: 0 0 12px 0;
    font-size: 1.25rem;
    font-weight: 600;
    color: #f9fafb;
  }

  h5 {
    margin: 0 0 8px 0;
    font-size: 1.1rem;
    font-weight: 600;
    color: #e5e7eb;
  }

  .top-section {
    display: flex;
    gap: 16px;
    margin-bottom: 16px;
    align-items: flex-start;
  }

  .preview-section {
    flex: 0 0 auto;
    padding: 12px;
    background: #1a1a1a;
    border-radius: 6px;
    overflow: hidden;
    display: flex;
    flex-direction: column;
  }

  .camera-wrapper {
    width: 640px;
    overflow: hidden;
  }

  .config-section {
    flex: 1 1 auto;
    min-width: 0;
  }

  .pose-table {
    width: auto;
    border-collapse: collapse;
    margin-bottom: 16px;
    table-layout: fixed;
  }

  .pose-table th {
    background: #2a2a2a;
    padding: 8px;
    text-align: center;
    font-weight: 600;
    font-size: 0.875rem;
    color: #e5e7eb;
    border: 1px solid #333;
    width: 20ch;
  }

  .pose-table td {
    padding: 4px;
    text-align: center;
    border: 1px solid #333;
    width: 20ch;
  }

  .pose-table input[type="number"] {
    width: 100%;
    padding: 6px 8px;
    border: 1px solid #333;
    border-radius: 4px;
    text-align: center;
    font-size: 0.95rem;
    background: #1a1a1a;
    color: #f9fafb;
  }

  .config-table {
    width: 100%;
    border-collapse: collapse;
    margin-bottom: 16px;
  }

  .config-table td.field {
    font-weight: 600;
    padding: 8px 12px;
    vertical-align: middle;
    width: 220px;
    color: #e5e7eb;
  }

  .config-table td.value {
    padding: 8px 12px;
  }

  .config-table input[type="number"],
  .config-table select {
    padding: 6px 8px;
    border: 1px solid #333;
    border-radius: 4px;
    width: 100%;
    max-width: 300px;
    font-size: 0.95rem;
    background: #1a1a1a;
    color: #f9fafb;
  }

  .config-table input[type="range"] {
    width: 200px;
    vertical-align: middle;
    accent-color: #dc2626;
  }

  .range-value {
    display: inline-block;
    min-width: 48px;
    margin-left: 8px;
    font-weight: 600;
  }

  .detection-section {
    margin-top: 16px;
  }

  .accordion-item {
    border: 1px solid #333;
    border-radius: 6px;
    margin-bottom: 8px;
    overflow: hidden;
  }

  .accordion-header {
    display: flex;
    align-items: center;
    gap: 8px;
    padding: 8px;
    background: #2a2a2a;
  }

  .accordion-header input[type="checkbox"] {
    width: 18px;
    height: 18px;
    cursor: pointer;
    accent-color: #dc2626;
  }

  .accordion-button {
    flex: 1;
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 8px;
    background: transparent;
    border: none;
    cursor: pointer;
    text-align: left;
    font-size: 1rem;
  }

  .accordion-button:hover {
    background: #333;
    border-radius: 4px;
  }

  .accordion-title {
    font-weight: 600;
    color: #f9fafb;
  }

  .accordion-icon {
    font-weight: bold;
    font-size: 1.2rem;
    color: #9ca3af;
  }

  .accordion-content {
    padding: 12px;
    background: #1a1a1a;
  }

  .disabled-message {
    padding: 12px;
    color: #9ca3af;
    font-style: italic;
    background: #1a1a1a;
    border-radius: 4px;
  }

  .warning-cell {
    padding: 8px 12px;
    color: #fbbf24;
    background: #292100;
    border-radius: 4px;
  }

  .actions {
    margin-top: 16px;
    display: flex;
    gap: 8px;
  }

  .primary {
    background: #dc2626;
    color: white;
    padding: 8px 16px;
    border-radius: 6px;
    border: none;
    cursor: pointer;
    font-size: 0.95rem;
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
    padding: 8px 16px;
    border-radius: 6px;
    cursor: pointer;
    font-size: 0.95rem;
  }

  .secondary:hover:not(:disabled) {
    background: #2a2a2a;
  }

  .secondary:disabled {
    opacity: 0.6;
    cursor: not-allowed;
  }
</style>
