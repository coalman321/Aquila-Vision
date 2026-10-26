import os
from typing import List
from abc import ABC, abstractmethod


class Description(ABC):
    def __init__(self) -> None:
        super().__init__()

    @abstractmethod
    def get_resolved_path(self) -> os.PathLike:
        """Get the resolved path to the description file."""
        raise NotImplementedError()

    @abstractmethod
    def load_file(self) -> list:
        """Load the sub description and return a list of processes it calls for."""
        raise NotImplementedError()
