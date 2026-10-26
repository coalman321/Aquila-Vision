import hashlib
from typing import List, Optional, Text, Tuple

from .process import Process
from .scheduling import Scheduling

import os.path
import yaml
import uuid

from ament_index_python import get_package_prefix


class Node(Process):
    def __init__(
        self,
        executable: str,
        package_name: str,
        node_name: str = "",
        namespace: str = "/",
        params: List = list(),
        args: List[str] = list(),
        remappings: List[Tuple[str, str]] = list(),
        scheduling: Optional[Scheduling] = None,
    ) -> None:
        super().__init__()

        self.scheduling = scheduling

        # lookup executable path
        package_path = get_package_prefix(package_name)
        self.executable_path = os.path.join(package_path, "lib", package_name, executable)
        if not os.path.isfile(self.executable_path):
            raise FileNotFoundError(f"Unable to locate executable {self.executable_path}")

        # setup CLI args, for some reason this needs a copy...
        self.args = args.copy()

        # deal with ros args
        self.args.append("--ros-args")

        # handle node name
        self.node_name = node_name
        if self.node_name == "":
            self.node_name = executable
        self.args.append("-r")
        self.args.append(f"__node:={self.node_name}")

        # handle the namespace
        self.args.append("-r")
        if len(namespace) != 0:
            # make sure its properly formatted
            if namespace[0] != "/":
                raise Exception(f"Invalid namespace {namespace}")
        self.args.append(f"__ns:={namespace}")
        if namespace != "/":
            namespace += "/"
        self.namespace = namespace

        # optionally add params
        if len(params) > 0:
            # create a params structure
            yaml_dict = {}
            params_files = []
            for param in params:
                if isinstance(param, Text):
                    params_files.append(param)
                else:
                    for key in list(param.keys()):
                        yaml_dict[key] = param[key]

            # if we have free params, create a params yaml file to load
            if len(yaml_dict) > 0:
                param_file_data = {f"/**": {"ros__parameters": yaml_dict}}
                yaml_data = yaml.dump(param_file_data)

                # make a sha1 hash of the content to keep from creating a bunch of random tempfiles
                content_hash = hashlib.sha1(yaml_data.encode("utf-8")).hexdigest()

                tmp_config = os.path.join("/", "tmp", f"{self.get_sanitized_name()}_{content_hash}_params.yaml")
                params_files.append(tmp_config)
                with open(tmp_config, "w+") as f:
                    f.write(yaml_data)

            # add the config files to the ros node
            for file in params_files:
                self.args.append("--params-file")
                self.args.append(file)

        # if we have remappings, add them
        for mapping in remappings:
            self.args.append("-r")
            self.args.append(f"{mapping[0]}:={mapping[1]}")

    def get_executable(self) -> str:
        """Get the full path to the executable binary to run."""
        return self.executable_path

    def get_args(self) -> List[str]:
        """Get the arguments to pass to the process on startup."""
        return self.args

    def get_scheduling(self) -> Optional[Scheduling]:
        """Get the scheduler settings for the node, None inherits them from the launch node."""
        return self.scheduling

    def get_type(self) -> str:
        """Get the process type of the action."""
        return "node"

    def get_name(self) -> str:
        """Get the ROS name for the node."""
        return f"{self.namespace}{self.node_name}"

    def get_sanitized_name(self) -> str:
        """Get a sanitized version of the ROS name without / characters."""
        return self.get_name().replace("/", "_")[1:]
