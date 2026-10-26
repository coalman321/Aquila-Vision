from typing import List, Optional
from abc import ABC, abstractmethod

from .scheduling import Scheduling


class Process(ABC):
    def __init__(self) -> None:
        super().__init__()

    @abstractmethod
    def get_executable(self) -> str:
        """Get the full path to the executable binary to run."""
        raise NotImplementedError()

    @abstractmethod
    def get_args(self) -> List[str]:
        """Get the arguments to pass to the process on startup."""
        raise NotImplementedError()

    @abstractmethod
    def get_type(self) -> str:
        """Get the process type of the action."""
        raise NotImplementedError()

    @abstractmethod
    def get_name(self) -> str:
        """Get the ROS name of the action."""
        raise NotImplementedError()

    @abstractmethod
    def get_sanitized_name(self) -> str:
        """Get the clean name of the action."""
        raise NotImplementedError()

    def get_scheduling(self) -> Optional[Scheduling]:
        """Get the scheduler settings for the process, None inherits them from the launch node."""
        return None
