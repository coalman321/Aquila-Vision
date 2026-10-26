import os
from .description import Description
from ..actions.process import Process
from ament_index_python import get_package_share_directory
from launch.launch_description_sources.python_launch_file_utilities import (
    load_python_launch_file_as_module,
)


class LoadDescription(Description):
    def __init__(self, package: os.PathLike, file: str = None):
        super().__init__()
        if os.path.isfile(package):
            self.resolved_path = package
        elif file != None:
            package_path = get_package_share_directory(package)
            self.resolved_path = os.path.join(package_path, "launch", file)
        else:
            raise FileNotFoundError(f"Unable to locate launch description {self.file}")

    def get_resolved_path(self) -> os.PathLike:
        """Get the resolved path to the description file."""
        return self.resolved_path

    def load_file(self) -> list:
        """Load the sub description and return a list of processes it calls for."""
        file_path = self.get_resolved_path()
        launch_file_module = load_python_launch_file_as_module(file_path)

        # validate its a proper file
        if not hasattr(launch_file_module, "generate_launch_description"):
            raise Exception(
                "launch file at '{}' does not contain the required function '{}'".format(
                    file_path, "generate_launch_description()"
                )
            )
        desc = getattr(launch_file_module, "generate_launch_description")()

        # filter the list in the description down to anyhting that inherits Process
        full_desc = []
        for item in desc:
            if isinstance(item, Process):
                full_desc.append(item)
            if isinstance(item, Description):
                full_desc.extend(item.load_file())

        return full_desc
