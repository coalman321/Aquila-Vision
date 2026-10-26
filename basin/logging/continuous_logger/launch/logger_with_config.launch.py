"""Launch file for continuous logger using a config file."""

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration, PathJoinSubstitution
from launch_ros.actions import Node
from launch_ros.substitutions import FindPackageShare


def generate_launch_description():
    # Find the package share directory
    pkg_share = FindPackageShare('continuous_logger')

    # Declare launch arguments
    config_file_arg = DeclareLaunchArgument(
        'config_file',
        default_value=PathJoinSubstitution([pkg_share, 'config', 'logger.yaml']),
        description='Path to the logger configuration file'
    )

    # Create the logger node
    logger_node = Node(
        package='continuous_logger',
        executable='logger_node',
        name='continuous_logger',
        output='screen',
        parameters=[LaunchConfiguration('config_file')]
    )

    return LaunchDescription([
        config_file_arg,
        logger_node,
    ])
