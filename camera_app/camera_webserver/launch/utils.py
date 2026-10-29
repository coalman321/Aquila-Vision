"""Common utility functions for camera_webserver launch files."""

import os
from typing import Union
from pathlib import Path


def get_app_dir(default_path: str = "/etc/app") -> Union[str, os.PathLike]:
    """
    Get the application directory for camera_webserver.
    
    Checks the CAMERA_WEBSERVER_APP_DIR environment variable first.
    If the directory doesn't exist or isn't writable, falls back to /tmp/camera_webserver/app.
    
    Args:
        default_path: The default directory to use if the environment variable is not set.
                     Defaults to "/opt/camera_webserver/app".
    
    Returns:
        The path to the application directory.
    """
    app_dir = os.getenv("CAMERA_WEBSERVER_APP_DIR", default_path)
    if not (os.path.exists(app_dir) and os.access(app_dir, os.W_OK)):
        print("Cannot access app directory at {}, falling back to /tmp".format(app_dir))
        app_dir = os.path.join("/", "tmp", "camera_webserver", "app")
        os.makedirs(app_dir, exist_ok=True)
    return app_dir

def get_log_dir(default_path: str = "/var/lib/aquila/logs") -> Union[str, os.PathLike]:
    """
    Get the directory the continuous logger writes its logs to.

    Checks the AQUILA_LOG_DIR environment variable first.
    If the directory doesn't exist or isn't writable, falls back to /tmp/camera_webserver/logs.

    Args:
        default_path: The default directory to use if the environment variable is not set.
                     Defaults to "/var/lib/aquila/logs", which setup-jetson.sh creates.

    Returns:
        The path to the log directory.
    """
    log_dir = os.getenv("AQUILA_LOG_DIR", default_path)
    if not (os.path.exists(log_dir) and os.access(log_dir, os.W_OK)):
        print("Cannot access log directory at {}, falling back to /tmp".format(log_dir))
        log_dir = os.path.join("/", "tmp", "camera_webserver", "logs")
        os.makedirs(log_dir, exist_ok=True)
    return log_dir

def get_team_number(app_dir: Union[str, os.PathLike]) -> int:
    """
    Get the team number from a file in the application directory.
    
    Args:
        app_dir: The application directory where the team file is located.

    Returns:
        The team number, or a default value if the file is not found or invalid.
    """
    team_file_path = os.path.join(app_dir, "team")
    team = 4143
    if os.path.isfile(team_file_path):
        with open(team_file_path, "r") as f:
            try:
                team = int(f.read().strip())
            except ValueError:
                pass
    return team
