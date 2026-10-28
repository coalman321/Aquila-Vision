import os
import sys
from pathlib import Path
from launch_api.actions.node import Node
from ament_index_python import get_package_share_directory

# Add launch directory to path for utils import
sys.path.insert(0, str(Path(__file__).parent))
from utils import get_app_dir, get_team_number


def generate_launch_description() -> list:
    # determine if this is in the app container by checking if the app dir exists and we have
    # permission to write to it. if we dont, fall back to /tmp
    app_dir = get_app_dir()
    team = get_team_number(app_dir)

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

    return list
