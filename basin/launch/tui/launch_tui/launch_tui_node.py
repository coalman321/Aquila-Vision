from copy import deepcopy
from typing import Any, Dict, List

from process_msgs.msg import ProcessInfo, ProcessList
from process_msgs.srv import RemoteRequest
from rclpy.context import Context
from rclpy.node import Node
from rclpy.parameter import Parameter
from rclpy.qos import qos_profile_system_default


class LaunchTuiNode(Node):
    def __init__(
        self,
        *,
        context: Context | None = None,
        cli_args: List[str] | None = None,
        namespace: str | None = None,
        use_global_arguments: bool = True,
        enable_rosout: bool = True,
        start_parameter_services: bool = True,
        parameter_overrides: List[Parameter] | None = None,
        allow_undeclared_parameters: bool = False,
        automatically_declare_parameters_from_overrides: bool = False,
        enable_logger_service: bool = False
    ) -> None:
        super().__init__(
            'launch_tui',
            context=context,
            cli_args=cli_args,
            namespace=namespace,
            use_global_arguments=use_global_arguments,
            enable_rosout=enable_rosout,
            start_parameter_services=start_parameter_services,
            parameter_overrides=parameter_overrides,
            allow_undeclared_parameters=allow_undeclared_parameters,
            automatically_declare_parameters_from_overrides=(
                automatically_declare_parameters_from_overrides
            ),
            enable_logger_service=enable_logger_service,
        )

        # subscribe to the status topic
        self.process_list_sub = self.create_subscription(
            ProcessList,
            '/state/launch/process_list',
            self.__process_list_callback,
            qos_profile_system_default,
        )

        # Track multiple systems
        # Dict[system_name, List[ProcessInfo]]
        self.system_processes: Dict[str, List[ProcessInfo]] = {}
        # Dict[system_name, config_name]
        self.system_configs: Dict[str, str] = {}
        # Dict[system_name, service_client]
        self.system_requesters: Dict[str, Any] = {}

        # Track pending requests per system
        # Dict[system_name, request]
        self.pending_requests: Dict[str, Any] = {}
        # Dict[system_name, future]
        self.request_futures: Dict[str, Any] = {}
        # Dict[system_name, response]
        self.responses: Dict[str, Any] = {}

    def __process_list_callback(self, msg: ProcessList) -> None:
        system_name = msg.system_name

        # Store processes for this system
        self.system_processes[system_name] = list(msg.processes)

        # Store config name for this system
        self.config_name = msg.active_config.split('/')[-1]
        self.system_configs[system_name] = self.config_name

        # Create service client for this system if it doesn't exist
        if system_name not in self.system_requesters:
            service_name = f'/state/launch/{system_name}/request'
            self.system_requesters[system_name] = self.create_client(
                RemoteRequest, service_name
            )
            self.get_logger().info(
                f'Created service client for system: {system_name} at {service_name}'
            )

    def handle_requests(self) -> bool:
        has_response = False

        # Process requests for each system
        for system_name in list(self.system_requesters.keys()):
            # Send pending request if not already sent
            if system_name in self.pending_requests and system_name not in self.request_futures:
                requester = self.system_requesters[system_name]
                request = self.pending_requests[system_name]
                self.request_futures[system_name] = requester.call_async(request)

            # Check for completed futures
            if system_name in self.request_futures and self.request_futures[system_name].done():
                self.responses[system_name] = self.request_futures[system_name].result()

                # Clean up request and future
                del self.pending_requests[system_name]
                del self.request_futures[system_name]
                has_response = True

        return has_response

    def get_request_result(self) -> RemoteRequest.Response | None:
        # Return any available response
        for system_name in list(self.responses.keys()):
            msg: RemoteRequest.Response = deepcopy(self.responses[system_name])
            del self.responses[system_name]
            return msg

        return None

    def has_new_data(self) -> bool:
        return len(self.system_processes) > 0

    def get_process_list(self) -> Dict[str, List[ProcessInfo]]:
        """Return dict mapping system_name to list of processes."""
        processes = deepcopy(self.system_processes)
        self.system_processes.clear()
        return processes

    def get_system_configs(self) -> Dict[str, str]:
        """Return dict mapping system_name to config_name."""
        return deepcopy(self.system_configs)

    def get_config_name(self) -> str:
        """Return config name (legacy method for single system)."""
        return self.config_name

    def send_launch_request(self, system_name: str) -> None:
        request = RemoteRequest.Request()
        request.request_id = RemoteRequest.Request.REQUEST_LAUNCH
        self.pending_requests[system_name] = request

    def send_shutdown_request(self, system_name: str) -> None:
        request = RemoteRequest.Request()
        request.request_id = RemoteRequest.Request.REQUEST_SHUTDOWN
        self.pending_requests[system_name] = request

    def send_start_request(self, system_name: str, process_id: int) -> None:
        request = RemoteRequest.Request()
        request.request_id = RemoteRequest.Request.REQUEST_START
        request.id_argument = process_id
        self.pending_requests[system_name] = request

    def send_stop_request(self, system_name: str, process_id: int) -> None:
        request = RemoteRequest.Request()
        request.request_id = RemoteRequest.Request.REQUEST_STOP
        request.id_argument = process_id
        self.pending_requests[system_name] = request

    def send_restart_request(self, system_name: str, process_id: int) -> None:
        request = RemoteRequest.Request()
        request.request_id = RemoteRequest.Request.REQUEST_RESTART
        request.id_argument = process_id
        self.pending_requests[system_name] = request

    def send_config_request(self, system_name: str, launch_name: str) -> None:
        request = RemoteRequest.Request()
        request.request_id = RemoteRequest.Request.REQUEST_LOAD_CONFIG
        request.scenario_argument = launch_name
        self.pending_requests[system_name] = request
