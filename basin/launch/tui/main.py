"""Bazel entry point for `bazel run //basin/launch/tui`.

Lives outside the launch_tui package so the package's relative imports work: a script inside it
would put launch_tui/ itself on sys.path, where launch_tui.py shadows the package.
"""

from launch_tui.launch_tui import main

main()
