from logging_msgs.msg import LogMetadata
import rclpy
from rclpy.duration import Duration
from rclpy.node import Node
from rclpy.qos import qos_profile_system_default
from ros2bag.verb import VerbExtension


class CreateTag(VerbExtension):
    """Connects to a production_log instance to add metadata to a given logger instance."""

    def add_arguments(self, parser, cli_name):  # noqa: D102
        parser.add_argument('tag_name', type=str, help='name of the tag to create')
        parser.add_argument('tag_value', type=str, help='data to store inside the tag')
        parser.add_argument(
            '--topic-name',
            type=str,
            default='/log/metadata',
            help='topic name to call the production log node via',
        )
        parser.add_argument(
            '--event',
            action='store_true',
            help='Set this flag to mark this metadata as a timed event',
        )

    def main(self, *, args):  # noqa: D102
        # init RCLPY
        rclpy.init()

        # create a temp node object to publish with
        node = Node('temp_metadata_node')
        publisher = node.create_publisher(LogMetadata, args.topic_name, qos_profile_system_default)

        # build the metadata message
        msg = LogMetadata()
        msg.header.stamp = node.get_clock().now().to_msg()
        msg.key_name = args.tag_name
        msg.yaml_metadata = args.tag_value
        msg.timed_event = args.event

        print(f'Sending metadata {msg}')

        # Spin once to make sure the publisher is registered
        rclpy.spin_once(node, timeout_sec=0.5)

        # send the message
        publisher.publish(msg)

        print('Metadata sent. Waiting up to 5s for ack...')

        # Spin until everyone has acked the message
        t_start = node.get_clock().now()
        while (
            not publisher.wait_for_all_acked(Duration(seconds=0.0))
            and (node.get_clock().now() - t_start) < Duration(seconds=5)
        ):
            rclpy.spin_once(node, timeout_sec=0.1)

        # shut RCLPY down
        rclpy.shutdown()
