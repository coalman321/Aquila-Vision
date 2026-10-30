"""Launch file for continuous logger with example configuration."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node


def generate_launch_description():
    # Declare launch arguments
    log_path_arg = DeclareLaunchArgument(
        'log_path',
        default_value='/tmp/continuous_logs',
        description='Path to the log directory'
    )

    log_split_time_arg = DeclareLaunchArgument(
        'log_split_time',
        default_value='60',
        description='Time between split files in seconds'
    )

    rotation_threshold_arg = DeclareLaunchArgument(
        'rotation_threshold_percent',
        default_value='80.0',
        description='Disk usage percentage (0-100) at which old logs are rotated away'
    )

    snapshot_usage_threshold_arg = DeclareLaunchArgument(
        'snapshot_usage_threshold_percent',
        default_value='50.0',
        description='Percentage of the disk (0-100) snapshot-protected logs may fill before a fault is raised'
    )

    # Create the logger node
    logger_node = Node(
        package='continuous_logger',
        executable='logger_node',
        name='continuous_logger',
        output='screen',
        parameters=[{
            'log_path': LaunchConfiguration('log_path'),
            'log_prefix': 'log_',
            'log_split_time': LaunchConfiguration('log_split_time'),
            'log_split_size': 2**63 - 1,  # max value
            'rotation_threshold_percent': LaunchConfiguration('rotation_threshold_percent'),
            'snapshot_usage_threshold_percent': LaunchConfiguration(
                'snapshot_usage_threshold_percent'
            ),
            'disk_check_interval': 60.0,
            # Example topic configuration
            'topics.rosout.name': '/rosout',
            'topics.rosout.throttle': 0,
            'topics.tf.name': '/tf',
            'topics.tf.throttle': 0,
            'topics.tf_static.name': '/tf_static',
            'topics.tf_static.throttle': 0,
        }]
    )

    return LaunchDescription([
        log_path_arg,
        log_split_time_arg,
        rotation_threshold_arg,
        snapshot_usage_threshold_arg,
        logger_node,
    ])
