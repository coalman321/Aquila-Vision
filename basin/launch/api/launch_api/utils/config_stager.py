import os.path
import yaml
import uuid
import shutil


class StageConfig:
    def __init__(self, overlay_dir: str, base_dir: str):
        self.overlay_dir = overlay_dir
        self.base_dir = base_dir

    def __clear_dir__(self, directory: os.PathLike) -> None:
        for root, dirs, files in os.walk(directory):
            for f in files:
                os.unlink(os.path.join(root, f))
            for d in dirs:
                shutil.rmtree(os.path.join(root, d))

    def stage_configs(self, runtime_dir: str):
        # first clear all configs in the runtime directory
        if os.path.isdir(runtime_dir):
            self.__clear_dir__(runtime_dir)
        else:
            os.makedirs(runtime_dir)

        # now walk the base dir and flatten it into the stage directory
        for root, _, files in os.walk(self.base_dir):
            for file in files:
                src_file = os.path.join(root, file)
                dest_file = os.path.join(runtime_dir, file)
                print(f"Staging {src_file} to  {dest_file}")
                shutil.copyfile(src_file, dest_file)

        # finally copy everything from the overlay
        for root, _, files in os.walk(self.overlay_dir):
            for file in files:
                src_file = os.path.join(root, file)
                dest_file = os.path.join(runtime_dir, file)
                print(f"Staging {src_file} to  {dest_file}")
                shutil.copyfile(src_file, dest_file)
