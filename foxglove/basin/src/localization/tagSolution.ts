// localization_msgs/msg/TagSolution -> foxglove.PoseInFrame, so the 3D panel can draw the multitag
// solution. Kept free of the extension API so it can be read (and tested) on its own.

export const TAG_SOLUTION_SCHEMA = "localization_msgs/msg/TagSolution";
export const POSE_IN_FRAME_SCHEMA = "foxglove.PoseInFrame";

// The solution is the camera's pose in the field. TagSolution's header is the camera image's, so its
// frame_id names the camera; the detector publishes the same pose as a PoseStamped in "field"
// (perception/apriltag_detector/landmark_detector_ros/src/detector_node.cpp), and so do we.
export const FIELD_FRAME = "field";

type Vector3 = { x: number; y: number; z: number };
type Quaternion = Vector3 & { w: number };

export type TagSolution = {
  header: { stamp: Record<string, unknown>; frame_id: string };
  pose: { position: Vector3; orientation: Quaternion };
  detected_tags: ArrayLike<number>;
};

export type PoseInFrame = {
  timestamp: { sec: number; nsec: number };
  frame_id: string;
  pose: { position: Vector3; orientation: Quaternion };
};

// builtin_interfaces/Time reaches the extension as { sec, nanosec }; older ROS-style times as { sec, nsec }.
function toTime(stamp: Record<string, unknown>): { sec: number; nsec: number } {
  return { sec: Number(stamp.sec ?? 0), nsec: Number(stamp.nanosec ?? stamp.nsec ?? 0) };
}

// A TagSolution with no tags carries an identity pose, not a solution; skip it rather than draw the
// camera at the field origin.
export function tagSolutionToPoseInFrame(msg: TagSolution): PoseInFrame | undefined {
  if (msg.detected_tags.length === 0) {
    return undefined;
  }
  const { position, orientation } = msg.pose;
  return {
    timestamp: toTime(msg.header.stamp),
    frame_id: FIELD_FRAME,
    pose: {
      position: { x: position.x, y: position.y, z: position.z },
      orientation: { x: orientation.x, y: orientation.y, z: orientation.z, w: orientation.w },
    },
  };
}
