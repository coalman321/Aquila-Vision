import { writable } from "svelte/store";

export interface SourceSummary {
    camera_type: string;
    camera_name: string;
    missing_calibration?: boolean;
}

/// Latest source list from the server, shared so the tab nav can flag sources that need attention
export const sourceSummaries = writable<SourceSummary[]>([]);

export async function fetchSourceSummaries(
    serverUrl: string = "",
    apiEndpoint: string = "/api/sources",
): Promise<SourceSummary[]> {
    const resp = await fetch(`${serverUrl}${apiEndpoint}`);
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const sources: SourceSummary[] = await resp.json();
    sourceSummaries.set(sources);
    return sources;
}
