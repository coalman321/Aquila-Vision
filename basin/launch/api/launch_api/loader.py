import os.path
from launch.launch_description_sources.python_launch_file_utilities import (
    load_python_launch_file_as_module,
)
from launch_api.descriptions.load_description import LoadDescription


class ProductionLoader:
    def __init__(self) -> None:
        pass

        self.desc = []

    def load_file(self, file: os.PathLike) -> None:
        loader = LoadDescription(file)
        self.desc = loader.load_file()

    def print_desc(self) -> None:
        for action in self.desc:
            print(
                f"{action.get_type()} -> {action.get_executable()}: {action.get_args()}"
            )

    def get_flat_desc(self) -> list:
        return self.desc

    def execute_in_python(self) -> None:

        import subprocess, time

        processes = []

        for action in self.desc:
            executable = [action.get_executable()]
            executable.extend(action.get_args())
            processes.append(subprocess.Popen(executable))

        time.sleep(10)

        for process in processes:
            process.kill()


if __name__ == "__main__":

    import ament_index_python

    loader = ProductionLoader()

    file = ament_index_python.get_package_share_directory("launch_api")
    file = os.path.join(file, "launch", "test.prod.py")
    print(f"Loading file {file}")

    loader.load_file(file)

    loader.print_desc()

    print("Executing temp launch")
    # loader.execute_in_python()
