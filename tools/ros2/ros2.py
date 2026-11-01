"""Entry point for //tools/ros2: runs ros2cli's own `main`, as the real `ros2` script does."""
import sys

from ros2cli.cli import main

if __name__ == '__main__':
    sys.exit(main())
