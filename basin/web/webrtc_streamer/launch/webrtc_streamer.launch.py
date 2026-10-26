from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    return LaunchDescription([
        Node(
            package='image_publisher_py',
            executable='image_publisher',
            name='image_publisher',
            output='screen',
            parameters=[
                {'publish_rate': 30.0},
                {'image_width': 1280},
                {'image_height': 960},
                {'topic_name': '/camera/image_raw'}
            ]
        ),
        Node(
            package='webrtc_streamer',
            executable='webrtc_streamer_node',
            name='webrtc_streamer',
            output='screen',
            parameters=[
                {'image_topic': '/camera/image_raw'},
                {'port': 8080} # Assuming web_core uses 'port' parameter, need to check
            ]
        )
    ])
