"""
Python bindings for log_util.

This package provides Python access to the C++ continuous logging library,
allowing users to query snapshots in ROS2 bag logs.
"""

import ctypes
import os
import sys


def _preload_rmw_implementation():
    """
    Load the configured RMW implementation before the extension is imported.

    The extension links librcl, whose static initializer dlopen()s the library named by
    RMW_IMPLEMENTATION while the extension itself is still being loaded. Under Bazel's hermetic
    toolchain that nested load throws std::bad_cast from inside libfastdds's own initialization
    and takes the interpreter down (librcl calls exit()). The same library loads fine on its own,
    and once it is resident the extension imports normally, so do that first. If the library
    can't be found here, leave it to librcl to report it, as before.
    """
    name = os.environ.get('RMW_IMPLEMENTATION')
    if not name:
        return
    suffix = '.dylib' if sys.platform == 'darwin' else '.so'
    try:
        ctypes.CDLL(f'lib{name}{suffix}', mode=ctypes.RTLD_GLOBAL)
    except OSError:
        pass


_preload_rmw_implementation()

from ._log_util_py import (  # noqa: E402
    DiskCheckResult,
    LogManager,
    SnapshotExtractor,
    SnapshotInfo,
    SnapshotWriter,
)

__all__ = [
    'SnapshotInfo',
    'SnapshotWriter',
    'SnapshotExtractor',
    'LogManager',
    'DiskCheckResult',
]

__version__ = '0.0.0'
