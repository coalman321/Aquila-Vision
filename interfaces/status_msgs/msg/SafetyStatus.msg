builtin_interfaces/Time stamp

uint8 stop_severity

uint8 SEVERITY_NONE = 0
uint8 SEVERITY_GLOBAL_PLAN_STOP = 1
uint8 SEVERITY_IMMEDIATE = 2

# Specifics on the current stop severity
bool is_killed
uint32[] asserting_switches
string[] registered_switches

bool is_faulted
FaultReport[] active_faults

