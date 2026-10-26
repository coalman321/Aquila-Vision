<script lang="ts">
  import { createEventDispatcher, onMount } from 'svelte';
  import { writable } from 'svelte/store';

  export let serverUrl: string = '';
  export let folder: string = ''; // Optional: not used by FileManagerComponent but kept for compatibility
  export let baseRoute: string = '/api/files'; // Matches FileManagerComponent config_.base_route
  // Optional full API endpoint that overrides serverUrl + baseRoute if present. Example: 'http://example.com/api/files' or '/api/files'
  export let apiEndpoint: string | null = null;
  export let usePutUpload = false; // If true, use PUT /baseRoute/:filename for uploads instead of POST
  export let allowedTypes: string[] | undefined = undefined; // undefined => accept any file type
  export let multiple = true;
  export let maxFileSizeMB = 20;
  export let hintText: string | undefined = undefined; // Optional: custom hint text shown in dropzone

  const dispatch = createEventDispatcher();
  let inputEl: HTMLInputElement | null = null;
  let uploadingFiles = writable([] as {name: string; progress: number; status: string}[]);
  let filesInFolder = writable([] as {name: string; url: string; size?: number; mtime?: number}[]);

  const normalizeBase = (u: string) => {
    if (!u) return '';
    return u.replace(/\/$/, '');
  };

  const ensureAbsoluteUrl = (u: string) => {
    if (!u) return '';
    // Already absolute if starts with scheme
    if (/^https?:\/\//i.test(u)) return u.replace(/\/$/, '');
    // If starts with a slash, treat as host-relative
    if (u.startsWith('/')) return u.replace(/\/$/, '');
    // Otherwise, treat as host:port and add http:// by default
    return `http://${u.replace(/\/$/, '')}`;
  };

  const normalizeRoute = (r: string) => {
    if (!r) return '';
    // Ensure route begins with a leading slash
    return r.startsWith('/') ? r.replace(/\/$/, '') : `/${r.replace(/\/$/, '')}`;
  };

  const getRouteUrl = () => {
    if (apiEndpoint) return ensureAbsoluteUrl(apiEndpoint);
    const base = normalizeBase(serverUrl);
    const route = normalizeRoute(baseRoute);
    const combined = base ? `${base}${route}` : `${route}`;
    return ensureAbsoluteUrl(combined);
  };

  const listFiles = async () => {
    const base = normalizeBase(serverUrl);
    const route = normalizeRoute(baseRoute);
    const baseUrl = apiEndpoint ? apiEndpoint.replace(/\/$/, '') : (base ? `${base}${route}` : `${route}`);
    // Listing does not require folder parameter in FileManagerComponent; we just call the base route
      const rel = `${route}`;
      // Build candidate URLs for fallback. Ensure they are absolute or relative as appropriate.
      const values: string[] = [];
      if (apiEndpoint) values.push(ensureAbsoluteUrl(apiEndpoint));
      if (base) values.push(ensureAbsoluteUrl(`${base}${rel}`));
      values.push(rel);
      const urls = values;
    for (const url of urls) {
      try {
        console.info('Listing files using', url);
        const res = await fetch(url);
        if (!res.ok) {
          const body = await res.text();
          console.warn('List files non-ok', { url, status: res.status, body });
          // try next if we have any
          continue;
        }
        const json = await res.json();
        const files = (json.files || []).map((f: any) => ({
          name: f.name,
          size: f.size,
          mtime: f.mtime,
            url: apiEndpoint ? ensureAbsoluteUrl(apiEndpoint) + `/${encodeURIComponent(f.name)}` : (base ? ensureAbsoluteUrl(`${base}${rel}`) + `/${encodeURIComponent(f.name)}` : `${rel}/${encodeURIComponent(f.name)}`)
        }));
        filesInFolder.set(files);
        return;
      } catch (e) {
        console.warn('List files error for', url, e);
        continue; // try next url (fallback)
      }
    }
    console.error('Failed to list files from any configured endpoint');
  };

  const handleDrop = (e: DragEvent) => {
    e.preventDefault();
    const dt = e.dataTransfer;
    if (!dt) return;
    const list = dt.files;
    uploadFiles(list);
  };

  const handleKeyDown = (e: KeyboardEvent) => {
    if (e.key === 'Enter' || e.key === ' ') {
      e.preventDefault();
      inputEl?.click();
    }
  };

  const handlePaste = (e: ClipboardEvent) => {
    const items = e.clipboardData?.files;
    if (items && items.length) uploadFiles(items);
  };

  const handleChange = (e: Event) => {
    const target = e.target as HTMLInputElement;
    if (target?.files?.length) uploadFiles(target.files);
  };

  function validType(file: File): boolean {
    if (!allowedTypes || allowedTypes.length === 0) return true;
    return allowedTypes.includes(file.type) || allowedTypes.includes(file.name.split('.').pop() || '');
  }

  function formatBytes(bytes?: number) {
    if (!bytes) return '';
    if (bytes === 0) return '0 B';
    const k = 1024;
    const dm = 2;
    const sizes = ['B', 'KB', 'MB', 'GB', 'TB'];
    const i = Math.floor(Math.log(bytes) / Math.log(k));
    return parseFloat((bytes / Math.pow(k, i)).toFixed(dm)) + ' ' + sizes[i];
  }

  function formatDate(epoch?: number) {
    if (!epoch) return '';
    const d = new Date(epoch * 1000);
    return d.toLocaleString();
  }

  async function uploadFiles(fileList: FileList | null) {
    if (!fileList) return;

    const files = Array.from(fileList);
    for (const file of files) {
      if (!validType(file)) {
        console.warn('Skipping file due to invalid type', file.name);
        continue;
      }
      if (file.size > maxFileSizeMB * 1024 * 1024) {
        console.warn('Skipping file too large', file.name);
        continue;
      }

      const uploadObj = { name: file.name, progress: 0, status: 'pending' };
      uploadingFiles.update(arr => [...arr, uploadObj]);

      try {
        const form = new FormData();
        form.append('file', file);
        // We keep folder for compatibility, if the server uses it otherwise it will be ignored.
        if (folder) form.append('folder', folder);
        const targetUrl = getRouteUrl();
        const xhr = new XMLHttpRequest();
        if (usePutUpload) {
          // PUT uploads will be performed per-file as streaming body; we use a plain body write for compatibility
          xhr.open('PUT', `${targetUrl}/${encodeURIComponent(file.name)}`);
          // For PUT we need to send the file bytes as the body, using send(file) directly.
        } else {
          xhr.open('POST', targetUrl);
        }
        xhr.upload.onprogress = (ev) => {
          if (ev.lengthComputable) {
            const pct = Math.round((ev.loaded / ev.total) * 100);
            uploadingFiles.update(arr => arr.map(a => a.name === file.name ? { ...a, progress: pct } : a));
          }
        };
        xhr.onload = async () => {
          if (xhr.status >= 200 && xhr.status < 300) {
            uploadingFiles.update(arr => arr.map(a => a.name === file.name ? { ...a, progress: 100, status: 'done' } : a));
            dispatch('uploaded', { file: file.name });
            // refresh list
            await listFiles();
          } else {
            console.error('Upload failed', xhr.responseText);
            uploadingFiles.update(arr => arr.map(a => a.name === file.name ? { ...a, status: 'error' } : a));
          }
        };
        xhr.onerror = () => {
          uploadingFiles.update(arr => arr.map(a => a.name === file.name ? { ...a, status: 'error' } : a));
        };
        if (usePutUpload) {
          // XMLHttpRequest with PUT; send file directly
          // NOTE: When using PUT, server expects raw body; do not send as multipart for this path
          const putXhr = new XMLHttpRequest();
          putXhr.open('PUT', `${targetUrl}/${encodeURIComponent(file.name)}`);
          putXhr.upload.onprogress = xhr.upload.onprogress;
          putXhr.onload = xhr.onload;
          putXhr.onerror = xhr.onerror;
          putXhr.send(file);
        } else {
          xhr.send(form);
        }
      } catch (e) {
        console.error('Upload error', e);
      }
    }
  }

  let renamingFile: string | null = null;
  let renameValue: string = '';

  function startRename(name: string) {
    renamingFile = name;
    renameValue = name;
  }

  function cancelRename() {
    renamingFile = null;
    renameValue = '';
  }

  async function renameFile(oldName: string) {
    const newName = renameValue.trim();
    if (!newName || newName === oldName) {
      cancelRename();
      return;
    }
    const baseUrl = getRouteUrl();
    const url = `${baseUrl}/${encodeURIComponent(oldName)}`;
    try {
      const res = await fetch(url, {
        method: 'PATCH',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ name: newName })
      });
      if (res.ok) {
        cancelRename();
        await listFiles();
        dispatch('renamed', { oldName, newName });
      } else {
        console.error('Failed to rename', await res.text());
      }
    } catch (e) {
      console.error('Rename file error', e);
    }
  }

  async function removeFile(name: string) {
    const baseUrl = getRouteUrl();
    const url = `${baseUrl}/${encodeURIComponent(name)}`;
    const ok = confirm(`Delete ${name}?`);
    if (!ok) return;
    try {
      const res = await fetch(url, { method: 'DELETE' });
      if (res.ok) {
        filesInFolder.update(files => files.filter(f => f.name !== name));
        dispatch('removed', { name });
        await new Promise(resolve => setTimeout(resolve, 250));
        await listFiles();
      } else {
        console.error('Failed to delete', await res.text());
      }
    } catch (e) {
      console.error('Delete file error', e);
    }
  }

  onMount(() => {
    // The cleanup has to be returned synchronously; Svelte ignores one from an async callback.
    listFiles();
    window.addEventListener('paste', handlePaste);
    return () => window.removeEventListener('paste', handlePaste);
  });
</script>

<div class="file-uploader">
  <div
    class="dropzone"
    on:drop|preventDefault={handleDrop}
    on:dragover|preventDefault
    on:click={() => inputEl?.click()}
    role="button"
    tabindex="0"
    on:keydown={handleKeyDown}
    title="Click or drop files here to upload"
  >
    <input
      type="file"
      bind:this={inputEl}
      {multiple}
      on:change={handleChange}
      accept={allowedTypes && allowedTypes.length ? allowedTypes.join(',') : undefined}
      style="display: none;"
    />

      <div class="drop-inner">
      <strong>Click or drop files here to upload</strong>
      <div class="hint">Allowed: {#if allowedTypes?.length}{allowedTypes.join(', ')}{:else}Any{/if}</div>
      {#if hintText}
        <div class="hint">{hintText}</div>
      {/if}
      <div class="hint">{#if folder}Folder: {folder}{/if}</div>
    </div>
  </div>

  <div class="uploads">
    <h3>Uploading</h3>
    {#each $uploadingFiles as item (item.name)}
      <div class="upload-item">
        <div class="name">{item.name}</div>
        <div class="progress-bar"><div class="inner" style="width: {item.progress}%"></div></div>
        <div class="status">{item.status}</div>
      </div>
    {/each}

    <h3>Files in folder</h3>
    <div class="files-list">
      {#each $filesInFolder as f}
        <div class="file-item">
          {#if renamingFile === f.name}
            <input
              class="rename-input"
              type="text"
              bind:value={renameValue}
              on:keydown={(e) => { if (e.key === 'Enter') renameFile(f.name); else if (e.key === 'Escape') cancelRename(); }}
              on:blur={() => renameFile(f.name)}
              autofocus
            />
            <div style="display: flex; gap: 6px;">
              <button class="save" on:click={() => renameFile(f.name)} title="Save">Save</button>
              <button class="cancel" on:click={cancelRename} title="Cancel">Cancel</button>
            </div>
          {:else}
            <div style="display: flex; gap: 10px; align-items: center;">
              <a href={f.url} target="_blank" rel="noopener">{f.name}</a>
              <div class="meta" style="color: var(--fw-text-muted, #6b7280); font-size: 0.875rem;">
                {#if f.size}{formatBytes(f.size)}{/if}
                {#if f.size && f.mtime}<span style="margin: 0 6px;">•</span>{/if}
                {#if f.mtime}{formatDate(f.mtime)}{/if}
              </div>
            </div>
            <div style="display: flex; gap: 6px;">
              <button class="rename" on:click={() => startRename(f.name)} title="Rename">Rename</button>
              <button class="remove" on:click={() => removeFile(f.name)} title="Delete">Delete</button>
            </div>
          {/if}
        </div>
      {/each}
      {#if $filesInFolder.length === 0}
        <div class="empty">No files uploaded yet.</div>
      {/if}
    </div>
  </div>
</div>

<style>
  .file-uploader { width: 520px; font-family: Inter, system-ui, -apple-system, 'Segoe UI', Roboto, 'Helvetica Neue', Arial; }
  .dropzone { border: 2px dashed var(--fw-border, #e5e7eb); background: var(--fw-bg, #fff); padding: 18px; cursor: pointer; border-radius: 8px; }
  .dropzone:hover { background: var(--fw-bg-hover, #f7fafc); }
  .drop-inner { text-align: center; color: var(--fw-text, #111827); }
  .hint { color: var(--fw-text-muted, #9ca3af); font-size: 0.875rem; }
  .uploads { margin-top: 12px; }
  .upload-item { display: flex; align-items: center; gap: 12px; margin: 6px 0; }
  .progress-bar { flex: 1; background: var(--fw-bg-subtle, #f3f4f6); border-radius: 6px; height: 8px; overflow: hidden; }
  .progress-bar .inner { background: var(--fw-progress, #10b981); height: 100%; transition: width 0.2s; }
  .files-list { margin-top: 10px; }
  .file-item { display: flex; gap: 8px; align-items: center; justify-content: space-between; padding: 8px; border-bottom: 1px solid var(--fw-border-subtle, #f3f4f6); }
  .file-item a { color: var(--fw-link, #2563eb); text-decoration: none; }
  .file-item .remove { background: var(--fw-danger, #ef4444); color: white; border: none; padding: 6px 8px; border-radius: 6px; cursor: pointer; }
  .file-item .rename { background: var(--fw-accent, #6366f1); color: white; border: none; padding: 6px 8px; border-radius: 6px; cursor: pointer; }
  .file-item .save { background: var(--fw-success, #10b981); color: white; border: none; padding: 6px 8px; border-radius: 6px; cursor: pointer; }
  .file-item .cancel { background: var(--fw-neutral, #6b7280); color: white; border: none; padding: 6px 8px; border-radius: 6px; cursor: pointer; }
  .rename-input { flex: 1; border: 1px solid var(--fw-border, #e5e7eb); border-radius: 6px; padding: 4px 8px; font-size: 0.9rem; }
  .empty { color: var(--fw-text-muted, #9ca3af); padding: 8px; }
</style>
