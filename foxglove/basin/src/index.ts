import { ExtensionContext } from "@foxglove/extension";

import { initFaultMonitorPanel } from "./faults/FaultMonitorPanel";
import { initLaunchControlPanel } from "./launch/LaunchControlPanel";
import { initContinuousLoggerPanel } from "./logger/ContinuousLoggerPanel";

export function activate(extensionContext: ExtensionContext): void {
  extensionContext.registerPanel({ name: "Launch Control", initPanel: initLaunchControlPanel });
  extensionContext.registerPanel({ name: "Fault Monitor", initPanel: initFaultMonitorPanel });
  extensionContext.registerPanel({ name: "Continuous Logger", initPanel: initContinuousLoggerPanel });
}
