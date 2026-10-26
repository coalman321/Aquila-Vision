// Reusable API interface and constants for launch management

export interface Process {
	process_id: number;
	process_name: string;
	// numeric status code matching ProcessInfo.msg (uint8 PROC_STAT_*)
	process_status: number;
	restart_counter: number;
	local_pid: number;
	process_cpu_pct: number;
	process_memory: number;
}

export interface SystemProcessList {
	system_name: string;
	active_config: string;
	processes: Process[];
}

export interface ProcessListResponse {
	[system_name: string]: SystemProcessList;
}

export interface RemoteRequest {
	system_name: string;
	request_id: number;
	id_argument: number;
	scenario_argument: string;
}

export interface RemoteResponse {
	resp_code: number;
	response_msg: string;
}

// Request types
export const REQUEST_NONE = 0;
export const REQUEST_START = 1;
export const REQUEST_STOP = 2;
export const REQUEST_RESTART = 3;
export const REQUEST_LOAD_CONFIG = 4;
export const REQUEST_LAUNCH = 5;
export const REQUEST_SHUTDOWN = 6;
export const REQUEST_INVALID = 7;

export const REQUEST_TYPES = [
	{ id: REQUEST_NONE, name: "None" },
	{ id: REQUEST_START, name: "Start" },
	{ id: REQUEST_STOP, name: "Stop" },
	{ id: REQUEST_RESTART, name: "Restart" },
	{ id: REQUEST_LOAD_CONFIG, name: "Load Config" },
	{ id: REQUEST_LAUNCH, name: "Launch" },
	{ id: REQUEST_SHUTDOWN, name: "Shutdown" },
	{ id: REQUEST_INVALID, name: "Invalid" },
];

// Process status constants (from ProcessInfo.msg)
export const PROC_STAT_INACTIVE = 0;
export const PROC_STAT_STARTING = 1;
export const PROC_STAT_RUNNING = 2;
export const PROC_STAT_TIMEOUT = 3;
export const PROC_STAT_STOPPING = 4;
export const PROC_STAT_RESTARTING = 5;
export const PROC_STAT_DEAD = 6;
export const PROC_STAT_INVALID = 7;

export const PROCESS_STATUS_NAMES: Record<number, string> = {
	[PROC_STAT_INACTIVE]: 'Inactive',
	[PROC_STAT_STARTING]: 'Starting',
	[PROC_STAT_RUNNING]: 'Running',
	[PROC_STAT_TIMEOUT]: 'Timeout',
	[PROC_STAT_STOPPING]: 'Stopping',
	[PROC_STAT_RESTARTING]: 'Restarting',
	[PROC_STAT_DEAD]: 'Dead',
	[PROC_STAT_INVALID]: 'Invalid',
};

// Response codes (from RemoteRequest.srv)
export const RESPONSE_OK = 0;
export const RESPONSE_REQ_INVALID = 1;
export const RESPONSE_INVALID_PROCESS_ID = 2;
export const RESPONSE_SYSTEM_ACTIVE = 3;
export const RESPONSE_SYSTEM_SHUTDOWN = 4;
export const RESPONSE_PROCESS_DEAD = 5;
export const RESPONSE_UNKNOWN_FAIL = 6;

export const RESPONSE_NAMES: Record<number, string> = {
	[RESPONSE_OK]: 'OK',
	[RESPONSE_REQ_INVALID]: 'REQ_INVALID',
	[RESPONSE_INVALID_PROCESS_ID]: 'INVALID_PROCESS_ID',
	[RESPONSE_SYSTEM_ACTIVE]: 'SYSTEM_ACTIVE',
	[RESPONSE_SYSTEM_SHUTDOWN]: 'SYSTEM_SHUTDOWN',
	[RESPONSE_PROCESS_DEAD]: 'PROCESS_DEAD',
	[RESPONSE_UNKNOWN_FAIL]: 'UNKNOWN_FAIL',
};
