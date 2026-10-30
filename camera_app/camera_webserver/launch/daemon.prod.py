import os
import sys
from pathlib import Path
from launch_api.actions.node import Node
from ament_index_python import get_package_share_directory

# Add launch directory to path for utils import
sys.path.insert(0, str(Path(__file__).parent))
from utils import get_app_dir, get_log_dir, get_team_number


def generate_launch_description() -> list:
    # determine if this is in the app container by checking if the app dir exists and we have
    # permission to write to it. if we dont, fall back to /tmp
    app_dir = get_app_dir()
    team = get_team_number(app_dir)
    # shared by the logger, which writes it, and the web UI, which downloads snapshots from it
    log_dir = get_log_dir()

    print(f"Detected team number: {team}")

    # convert team number to ip address by splitting into octets and joining with dots
    team_str = str(team).zfill(4)
    rio_ip = f"10.{team_str[0:2]}.{team_str[2:]}.2"

    list = []

    list.append(
        Node(
            package_name="camera_webserver",
            executable="camera_web",
            params=[
                {
                    "app_dir": app_dir,
                    "log_dir": log_dir,
                    "port": 5800,
                    "enable_static_content": True,
                    "field": {
                        "base_frame": "field",
                        "child_prefix": "",
                    },
                    "camera": {"base_frame": "base_link"},
                }
            ],
        )
    )

    list.append(
        Node(
            package_name="chassis_proxy_client",
            executable="proxy_client",
            params=[
                {
                    "server_addr": rio_ip,
                    "server_port": 5809,
                    "client_port": 5809,
                    "tick_rate": 0.02,

                    "detections_topic": "/camera_webserver/detections",
                    "camera_info_topic": "/camera_webserver/camera_info",
                    "tag_solution_topic": "/camera_webserver/tag_solutions",

                    "robot_base_frame": "base_link",
                }
            ],
        )
    )

    list.append(
        Node(
            package_name="webrtc_streamer",
            executable="webrtc_streamer_node",
            params=[
                {
                    "port": 5801,
                    "bandwidth": {
                        "encoder_speed": 4,
                        "max_framerate": 5,
                        "resolution_scale": 0.5,
                        "target_bitrate": 500000,
                    },
                    "discovery": {
                        "patterns": ["/driver/image_raw", "/debug_img", "/debug_image", "/driver/image_rect"]
                    },
                }
            ],
            args=["--ros-args", "--log-level", "warn"],
        )
    )

    list.append(
        Node(
            package_name="camera_cal",
            executable="camera_cal_node",
            params=[
                {
                    "output_dir": str(Path(app_dir) / "calibrations"),
                    "image_topic": "",
                    "camera_info_topic": "/camera_info",
                    "base_frame": "base_link",
                    # Board type and geometry come with each start request (CalibrationManager.svelte).
                }
            ],
            # args=["--ros-args", "--log-level", "warn"],
        )
    )

    # Started with the daemons so it records across camera pipeline reloads. A `*` in a topic name matches
    # one segment (a camera's namespace), and the logger keeps looking, so cameras enabled later are logged.
    list.append(
        Node(
            package_name="continuous_logger",
            executable="continuous_logger_node",
            params=[
                {
                    "log_path": log_dir,
                    "log_split_size": 100 * 1024 * 1024,  # 100 MB
                    "log_split_time": 30,  # split logs every 30 seconds
                    "rotation_threshold_percent": 60.0, # Threshold to begin rotation (effective max disk usage)
                    "snapshot_usage_threshold_percent": 40.0,  # Threshold for snapshot disk usage before faulting
                    # how often to rotate and re-measure the logs, which is how often the web UI's Logger tab
                    # sees log and snapshot usage change (the default is 60 s)
                    "disk_check_interval": 5.0,

                    # throttle is to log 1 in every N+1 messages. Set to 0 to log all messages
                    "topics": {
                        "tag_solutions": {"name": "/camera_webserver/tag_solutions", "throttle": 0},
                        # only want every few of these (2hz for side and 4hz front)
                        "debug_image": {"name": "/*/apriltag_detector/debug_image", "throttle": 8},
                        "camera_info": {"name": "/*/driver/camera_info", "throttle": 0},
                        "rosout": {"name": "/rosout", "throttle": 0},
                        "tf": {"name": "/tf", "throttle": 0},
                        "tf_static": {"name": "/tf_static", "throttle": 0},
                    },

                    "event_snapshots": {
                        "match_start": {
                            "event_name": "match_start",
                            "time_before": 10.0,
                            "time_after": 2.5 * 60.0,  # 2.5 minutes after match start
                            "tags": ["match", "frc"],
                            "description": "",
                        },
                    },
                }
            ],
        )
    )

    return list
