import time

from logging_msgs.srv import SnapshotRequest
import rclpy
from rclpy.node import Node
from ros2bag.verb import VerbExtension


class CreateSnapshot(VerbExtension):
    """Connects to a production_log instance to create a log snapshot."""

    def add_arguments(self, parser, cli_name):  # noqa: D102
        parser.add_argument(
            'snapshot_name', type=str, help='name of the snapshot to create'
        )
        parser.add_argument(
            '--service-name',
            type=str,
            default='/log/snapshot',
            help='service name to call the production log node via',
        )
        parser.add_argument(
            '--description',
            type=str,
            default='manual',
            help='description for the snapshot',
        )
        parser.add_argument(
            '--tags',
            type=str,
            nargs='*',
            default=[],
            help='tags to associate with this snapshot',
        )

    def send_request(self, service_name: str, request: SnapshotRequest.Request) -> bool:
        # init RCLPY
        rclpy.init()

        # create a temp node object to publish with
        node = Node('temp_snapshot_node')
        client = node.create_client(SnapshotRequest, service_name)

        # make sure the server is still alive
        retries = 10
        while not client.service_is_ready() and retries > 0:
            print(f'Verifying service {service_name} is alive')
            retries -= 1
            client.wait_for_service(0.25)
            if retries == 0:
                print(f'Waiting for service {service_name} failed')
                return False

        # send the request
        future = client.call_async(request)
        rclpy.spin_until_future_complete(node, future)

        # make sure the request is ok otherwise print the error
        response: SnapshotRequest.Response
        response = future.result()
        if response.resp_code != SnapshotRequest.Response.RESPONSE_OK:
            print(response.response_msg)
            return False

        # shut RCLPY down
        rclpy.shutdown()

        return True

    def main(self, *, args):  # noqa: D102
        # create the startup request
        start_req = SnapshotRequest.Request()
        start_req.request_id = SnapshotRequest.Request.REQUEST_START
        start_req.snapshot_name = args.snapshot_name
        start_req.description = args.description
        start_req.tags = args.tags

        # create a stop request
        stop_req = SnapshotRequest.Request()
        stop_req.request_id = SnapshotRequest.Request.REQUEST_STOP
        stop_req.snapshot_name = args.snapshot_name

        if not self.send_request(args.service_name, start_req):
            return 1

        print(f'Started snapshot {args.snapshot_name}')

        print('\nPress ^C to stop the snapshot')
        try:
            while True:
                time.sleep(1.0)
        except KeyboardInterrupt:
            pass

        if not self.send_request(args.service_name, stop_req):
            return 1

        print(f'\nStopped snapshot {args.snapshot_name}')
