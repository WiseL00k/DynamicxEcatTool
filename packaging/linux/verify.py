import argparse
import json
import os
from pathlib import Path
import subprocess

from common import run, sha256, ubuntu_version


def launch(command, log):
    environment = dict(os.environ, QT_QPA_PLATFORM="offscreen", QT_QUICK_BACKEND="software")
    for variable in ("LD_LIBRARY_PATH", "QT_PLUGIN_PATH", "QML_IMPORT_PATH", "QML2_IMPORT_PATH"):
        environment.pop(variable, None)
    with log.open("w") as stream:
        process = subprocess.Popen([str(command)], env=environment, stdout=stream, stderr=stream)
        try:
            process.wait(timeout=8)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        else:
            raise ValueError("Packaged application exited during startup: " + log.read_text())
    text = log.read_text()
    if any(error in text for error in ("failed to load component", "is not installed", "cannot load library")):
        raise ValueError("QML/plugin startup failure: " + text)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--packages", type=Path, required=True)
    args = parser.parse_args()
    packages = args.packages.resolve()
    manifest = json.loads((packages / "build-info.json").read_text())
    if manifest["ubuntu"] != ubuntu_version():
        raise ValueError("Wrong Ubuntu package for this validation container")
    for artifact in manifest["artifacts"]:
        path = packages / artifact["name"]
        if path.parent != packages or sha256(path) != artifact["sha256"]:
            raise ValueError("Package SHA256 mismatch")
    run("sha256sum", "--check", "SHA256SUMS", cwd=packages)
    deb = next(packages.glob("*.deb"))
    run("apt-get", "update")
    run("apt-get", "install", "-y", "--no-install-recommends", deb)
    # This job starts in a clean base image with no build Qt or yaml-cpp installed.
    launch(Path("/usr/bin/dynamicx-ecat-tool"), packages / "deb-startup.log")
    image = next(packages.glob("*.AppImage"))
    image.chmod(0o755)
    run(image, "--appimage-extract", cwd=packages, stdout=subprocess.DEVNULL)
    launch(packages / "squashfs-root/AppRun", packages / "appimage-startup.log")
    run("apt-get", "remove", "-y", "dynamicx-ecat-tool")
    print("PASS: checksums, deb install/remove, AppImage payload startup; no hardware communication")


if __name__ == "__main__":
    main()
