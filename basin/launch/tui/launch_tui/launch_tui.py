import rclpy

from .launch_tui_app import LaunchApp


def main():
    """Launch the launch TUI application."""
    rclpy.init()

    LaunchApp().run()

    rclpy.shutdown()


if __name__ == '__main__':
    main()
