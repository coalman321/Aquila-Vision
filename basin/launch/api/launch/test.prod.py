import os
from launch_api.actions.node import Node
from launch_api.descriptions.load_description import LoadDescription
from launch_api.utils.config_stager import StageConfig
from ament_index_python import get_package_share_directory


def generate_launch_description() -> list:
    # base_configs = os.path.join(get_package_share_directory("chassis_app"), "default_config")
    # overlay_dir = os.path.join("/", "tmp", "overlay")
    # load_dir = os.path.join("/", "tmp", "config_ws")

    # cfg_stage = StageConfig(overlay_dir, base_configs)
    # cfg_stage.stage_configs(load_dir)

    return [
        Node(
            "talker",
            "demo_nodes_cpp",
            namespace="/durr",
            params=[],
        ),
        Node(
            "listener",
            "demo_nodes_cpp",
            namespace="/durr",
            params=[],
        ),
        LoadDescription(
            "launch_api",
            "test2.prod.py"
        )
    ]
