import os
import sys
from launch_api.actions.node import Node
from ament_index_python import get_package_share_directory
from pathlib import Path

from json import load as json_load

# Add launch directory to path for utils import
sys.path.insert(0, str(Path(__file__).parent))
from utils import get_app_dir


def create_pipeline(filename: os.PathLike, source_config: dict) -> list:
    # load over each pipeline in the pipelines dir
    pipelines_dir = os.path.join(get_app_dir("/etc/app"), "pipelines")
    if not os.path.exists(pipelines_dir):
        os.makedirs(pipelines_dir, exist_ok=True)

    nodes = []

    # find the matching pipeline for this source :)
    print(f"finding pipeline {filename}")
    pipeline_path = Path(os.path.join(pipelines_dir, filename))
    if not os.path.isfile(pipeline_path):
        print("Unable to load pipeline")
    else:
        camera_name = pipeline_path.stem  # filename without extension
        print(f"Loading pipeline config: {camera_name} from {pipeline_path}")

        pipeline_config = {}
        try:
            with open(pipeline_path, "r") as f:
                pipeline_config = json_load(f)
        except Exception as e:
            print(f"Failed to load pipeline config from {pipeline_path}: {e}")

        calibration_file = os.path.join(get_app_dir(), "calibrations", source_config.get("calibration_file"))
        print(f"using cal file {calibration_file}")

        nodes.append(
            Node(
                package_name="basler_driver",
                executable="basler_driver",
                node_name="driver",
                namespace="/" + camera_name,
                params=[
                    {
                        # Open the device we want by serial number
                        "device_serial": source_config.get("camera_name", ""),
                        "camera_frame": camera_name,
                        "camera_info_url": "file://" + calibration_file,
                        # configure image settings
                        "exposure_auto": False,
                        "gain_auto": False,
                        "gain": pipeline_config.get("gain", 0.5),
                        "exposure": pipeline_config.get("exposure", 1000.0),
                        "image_encoding": "rgb8",
                        "frame_rate": float(source_config.get("frame_rate", 30.0)),
                        "shutter_mode": "global",
                        "binning_x": source_config.get("binning", 1),
                        "binning_y": source_config.get("binning", 1),
                        "grab_strategy": 1,
                    }
                ],
                args=["--ros-args", "--log-level", "info"],
            )
        )

        # Load the apriltag detector
        apriltag_config = pipeline_config.get("apriltag_config")
        nodes.append(
            Node(
                package_name="landmark_detector_ros",
                executable="detector_node",
                node_name=f"apriltag_detector",
                namespace="/" + camera_name,
                params=[
                    {
                        "camera_topic": "driver/image_raw",
                        "enabled": apriltag_config.get("enabled"),
                        "is_multitag": True,
                        "tag_edge_length": apriltag_config.get("tag_size"),
                        "parent_frame": "field",
                    }
                ],
                remappings=[("/" + camera_name + "/apriltag_detector/tag_solution", "/camera_webserver/tag_solutions")],
                args=["--ros-args", "--log-level", "info"],
            )
        )

        # Load the object detector
        # object_enabled = pipeline_config["detection_config"]["enabled"]
        # nodes.append(
        #     Node(
        #         package_name="object_detector_ros",
        #         executable="object_detector_node",
        #         node_name=f"object_detector",
        #         namespace="/" + camera_name,
        #         params=[{"cam_info_topic": "driver/camera_info", "image_topic": "driver/image_raw", "enabled": object_enabled}],
        #     )
        # )

    return nodes


def generate_launch_description() -> list:
    app_dir = get_app_dir()
    list = []

    # iterate over the .json config files in the app's camera_sources dir
    source_cfg_dir = os.path.join(app_dir, "camera_sources")
    if not os.path.exists(source_cfg_dir):
        os.makedirs(source_cfg_dir, exist_ok=True)

    print(f"using camera source config dir: {source_cfg_dir}")

    for filename in os.listdir(source_cfg_dir):
        if filename.endswith(".json"):
            full_path = os.path.join(source_cfg_dir, filename)

            print(f"Loading camera source config: {full_path}")

            # read the source config and load its nodes if enabled
            with open(full_path, "r") as f:
                cfg = json_load(f)
                if "source_enabled" in cfg and cfg["source_enabled"]:
                    print(f"Creating pipeline for source config: {full_path}")
                    list.extend(create_pipeline(filename, cfg))

    # Log events
    events = [
        {
            "event_name": "match_start",
            "time_before": 5.0,
            "time_after": 2.5*60.0,  # 2.5 minutes after match start
            "description": "",
            "tags": ["match", "frc"]
        }
    ]

    # throttle is to log 1 in every N messages. Set to 0 to log all messages
    topics = [
        {
            "name": "/camera_webserver/tag_solutions",
            "throttle": 0,
        },
        {
            "name": "/*/apriltag_detector/debug_image",
            "throttle": 8, # only want every few of these (2hz for side and 4hz front)
        },
        {
            "name": "/*/driver/camera_info",
            "throttle": 0,
        }
    ]

    # # configure logging
    # list.append(
    #     Node(
    #         package_name="continuous_logger",
    #         executable="continuous_logger_node",
    #         params=[
    #             {
    #                 "log_path": str(Path(app_dir) / "logs"),
    #                 "log_split_size": 100 * 1024 * 1024,  # 100 MB
    #                 "log_split_time": 30,  # split logs every 30 seconds
    #                 "max_disk_usage_percent": 75,  # if disk usage exceeds 75%, start deleting old logs
    #                 "disk_warning_threshold_percent": 80,  # if disk usage exceeds 80%, log a warning

    #                 "event_keys": [event["event_name"] for event in events],
    #                 "events": events,

    #                 "topic_keys": [topic["name"] for topic in topics],
    #                 "topics": topics,
    #             }
    #         ],
    #         # args=["--ros-args", "--log-level", "warn"],
    #     )
    # )

    return list
