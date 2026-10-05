import datetime
import os
import shutil
import subprocess


def build_engine(source_dir, copy_to):
    subprocess.run(["make", "-C", source_dir, "release"], check=True, stdout=subprocess.DEVNULL)
    shutil.copy2(os.path.join(source_dir, "bin", "chess"), copy_to)


def make_timestamped_dir(parent, name):
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    path = os.path.join(os.path.abspath(parent), f"{stamp}-{name}")
    suffix = 1
    while True:
        try:
            os.makedirs(path if suffix == 1 else f"{path}-{suffix}")
            return path if suffix == 1 else f"{path}-{suffix}"
        except FileExistsError:
            suffix += 1
