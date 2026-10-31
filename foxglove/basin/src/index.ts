import { ExtensionContext } from "@foxglove/extension";

import { initFaultMonitorPanel } from "./faults/FaultMonitorPanel";
import { initLaunchControlPanel } from "./launch/LaunchControlPanel";
import {
  POSE_IN_FRAME_SCHEMA,
  TAG_SOLUTION_SCHEMA,
  TagSolution,
  tagSolutionToPoseInFrame,
} from "./localization/tagSolution";
import { initContinuousLoggerPanel } from "./logger/ContinuousLoggerPanel";

export function activate(extensionContext: ExtensionContext): void {
  extensionContext.registerPanel({ name: "Launch Control", initPanel: initLaunchControlPanel });
  extensionContext.registerPanel({ name: "Fault Monitor", initPanel: initFaultMonitorPanel });
  extensionContext.registerPanel({ name: "Continuous Logger", initPanel: initContinuousLoggerPanel });
  extensionContext.registerMessageConverter<TagSolution>({
    type: "schema",
    fromSchemaName: TAG_SOLUTION_SCHEMA,
    toSchemaName: POSE_IN_FRAME_SCHEMA,
    converter: tagSolutionToPoseInFrame,
  });
}
