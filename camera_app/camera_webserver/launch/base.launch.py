from launch import LaunchDescription 
from launch.actions import ExecuteProcess, RegisterEventHandler, Shutdown, DeclareLaunchArgument 
from launch.event_handlers import OnProcessExit 
from launch_ros.actions import Node 
from launch.substitutions import LaunchConfiguration 
from pathlib import Path 
from ament_index_python.packages import get_package_share_directory

def generate_launch_description():
    # Determine production launch files
    launch_dir = Path(get_package_share_directory('camera_webserver')) / 'launch'
    app_file = str(launch_dir / 'application.prod.py')
    daemon_file = str(launch_dir / 'daemon.prod.py')

    # setup the production launch node
    launcher = Node(
        package='production_launch',
        executable='production_launch',
        output='screen',
        parameters=[{
            # packages
            'launch_package': 'camera_webserver',
            'daemon_package': 'camera_webserver',

            'launch_file': app_file,
            'daemon_file': daemon_file,
        }],
    )

    return LaunchDescription([
        launcher,
    ])

