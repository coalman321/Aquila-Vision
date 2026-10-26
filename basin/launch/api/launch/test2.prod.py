from launch_api.actions.node import Node
from launch_api.actions.scheduling import Scheduling


def generate_launch_description() -> list:

    return [
        Node(
            "talker",
            "demo_nodes_cpp",
            namespace="/hurr",
            params=[{"something": 1}],
            scheduling=Scheduling("batch", nice=5),
        ),

        # Node(
        #     "listener",
        #     "demo_nodes_cpp",
        #     params=[{"something": 1}, "/path/to/some/config.yaml"],
        # ),
    ]