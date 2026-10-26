from launch import LaunchDescription
from launch_ros.actions import Node

def generate_launch_description():
    """
    Example launch file demonstrating multiple WebRTC streams.
    
    This configuration creates two image publishers and one WebRTC streamer
    that handles both streams. Clients can connect to either stream by
    specifying the stream_id in the WebRTC offer.
    
    Streams:
    - "camera1" -> /camera1/image_raw
    - "camera2" -> /camera2/image_raw
    """
    return LaunchDescription([
        # First image publisher (camera1)
        Node(
            package='image_publisher_py',
            executable='image_publisher',
            name='image_publisher_1',
            output='screen',
            parameters=[
                {'publish_rate': 30.0},
                {'image_width': 1280},
                {'image_height': 960},
                {'topic_name': '/camera1/image_raw'}
            ]
        ),
        # Second image publisher (camera2)
        Node(
            package='image_publisher_py',
            executable='image_publisher',
            name='image_publisher_2',
            output='screen',
            parameters=[
                {'publish_rate': 30.0},
                {'image_width': 1280},
                {'image_height': 960},
                {'topic_name': '/camera2/image_raw'}
            ]
        ),
        # WebRTC streamer with multiple streams
        Node(
            package='webrtc_streamer',
            executable='webrtc_streamer_node',
            name='webrtc_streamer',
            output='screen',
            parameters=[
                {
                    'streams': [
                        'camera1:/camera1/image_raw',
                        'camera2:/camera2/image_raw'
                    ]
                },
                {'port': 8080},
                {'target_bitrate': 2000000},
                {'max_framerate': 30},
                {'resolution_scale': 1.0},
                {'encoder_speed': 4}
            ]
        )
    ])
