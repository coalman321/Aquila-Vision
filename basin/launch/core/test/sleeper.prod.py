"""Launch description used by test_launch_node.py: a single long-running child process."""
from launch_api.actions.process import Process


class Sleeper(Process):
    def get_executable(self):
        return "/bin/sleep"

    def get_args(self):
        # Far longer than the test runs, so it can only end by being shut down by the node.
        return ["300"]

    def get_type(self):
        return "process"

    def get_name(self):
        return "launch_node_test_sleeper"

    def get_sanitized_name(self):
        return "launch_node_test_sleeper"


def generate_launch_description():
    return [Sleeper()]
