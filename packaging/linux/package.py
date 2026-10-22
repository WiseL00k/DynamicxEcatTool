import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from common import DEPENDENCIES, ROOT, download, output, run, sha256, ubuntu_version, version


def executable(path, text):
    path.write_text(text)
    path.chmod(0o755)


def extract_tool(name, spec, tools):
    image = download(spec, tools / (name + ".AppImage"))
    image.chmod(0o755)
    directory = tools / name
    directory.mkdir()
    run(image, "--appimage-extract", cwd=directory, stdout=subprocess.DEVNULL)
    return directory / "squashfs-root/AppRun"


def system_dependencies(appdir):
    """Use actual ELF resolution to exclude bundled libs from deb dependencies."""
    packages = set()
    environment = dict(os.environ, LD_LIBRARY_PATH=str(appdir / "usr/lib"))
    for path in appdir.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        with path.open("rb") as stream:
            if stream.read(4) != b"\x7fELF":
                continue
        libraries = output("ldd", path, env=environment)
        if "not found" in libraries:
            raise ValueError("Unresolved library for " + str(path) + "\n" + libraries)
        for match in re.finditer(r"=> (/\S+)", libraries):
            resolved = Path(match.group(1))
            if appdir in resolved.resolve().parents:
                continue
            candidates = [str(resolved), str(resolved.resolve())]
            if str(resolved).startswith("/usr/lib/"):
                candidates.append(str(resolved)[4:])
            elif str(resolved).startswith("/lib/"):
                candidates.append("/usr" + str(resolved))
            for candidate in candidates:
                result = subprocess.run(["dpkg-query", "-S", candidate], text=True, capture_output=True)
                if result.returncode == 0:
                    packages.add(result.stdout.split(": ", 1)[0].split(":", 1)[0])
                    break
            else:
                raise ValueError("Cannot identify system dependency " + str(resolved))
    dependencies = []
    for package in sorted(packages):
        installed = output("dpkg-query", "-W", "-f=${Version}", package)
        dependencies.append(package + " (>= " + installed + ")")
    return ", ".join(dependencies)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--prefix", type=Path, required=True)
    parser.add_argument("--build", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    release, ubuntu = version(), ubuntu_version()
    prefix, build, destination = args.prefix.resolve(), args.build.resolve(), args.output.resolve()
    if destination.exists():
        raise ValueError("Use a fresh output directory")
    tools, appdir, publish = destination / "tools", destination / "AppDir", destination / "publish"
    tools.mkdir(parents=True)
    publish.mkdir()
    deploy = extract_tool("linuxdeploy", DEPENDENCIES["tools"]["linuxdeploy"], tools)
    plugin = extract_tool("qt-plugin", DEPENDENCIES["tools"]["linuxdeploy-plugin-qt"], tools)
    appimagetool = extract_tool("appimagetool", DEPENDENCIES["tools"]["appimagetool"], tools)
    runtime = download(DEPENDENCIES["tools"]["runtime"], tools / "runtime-x86_64")
    executable(tools / "linuxdeploy-plugin-qt", '#!/bin/sh\nexec "' + str(plugin) + '" "$@"\n')
    environment = dict(os.environ,
        PATH=str(tools) + ":" + str(prefix / "qt/bin") + ":" + os.environ["PATH"],
        LD_LIBRARY_PATH=":".join(map(str, [prefix / "qt/lib", prefix / "yaml/lib"])),
        QMAKE=str(prefix / "qt/bin/qmake"), QML_SOURCES_PATHS=str(ROOT / "qml"),
        EXTRA_QT_MODULES="svg", EXTRA_PLATFORM_PLUGINS="libqoffscreen.so;libqminimal.so")
    icon = tools / "dynamicx-ecat-tool.svg"
    shutil.copy2(ROOT / "icons/logo-mark.svg", icon)
    run(deploy, "--appdir", appdir, "--executable", build / "DynamicxEcatTool",
        "--library", build / "SOEM_interface/libsoem_interface.so",
        "--desktop-file", Path(__file__).with_name("dynamicx-ecat-tool.desktop"),
        "--icon-file", icon, "--plugin", "qt", env=environment)
    # linuxdeploy may create AppRun as a symlink to the application. Never
    # follow that symlink when replacing it with the portable launcher.
    (appdir / "AppRun").unlink(missing_ok=True)
    shutil.copy2(Path(__file__).with_name("AppRun"), appdir / "AppRun")
    (appdir / "AppRun").chmod(0o755)
    with (appdir / "usr/bin/DynamicxEcatTool").open("rb") as stream:
        if stream.read(4) != b"\x7fELF":
            raise ValueError("Deployed application is not an ELF executable")
    (appdir / "usr/bin/qt.conf").write_text("[Paths]\nPrefix=..\nPlugins=plugins\nQmlImports=qml\n")
    for directory in ("esi", "sample_config"):
        shutil.copytree(ROOT / "config" / directory, appdir / "usr/bin" / directory)
    for required in ("usr/bin/DynamicxEcatTool", "usr/lib/libsoem_interface.so",
                     "usr/plugins/platforms/libqxcb.so", "usr/plugins/platforms/libqoffscreen.so",
                     "usr/plugins/imageformats/libqsvg.so", "usr/qml/QtQuick/Controls/qmldir",
                     "usr/bin/esi/ECAT2CANFD.xml"):
        if not (appdir / required).is_file():
            raise ValueError("Deployment missing " + required)
    # No root-only capabilities, setuid flags or driver installation are embedded.
    run("desktop-file-validate", appdir / "usr/share/applications/dynamicx-ecat-tool.desktop")
    dependencies = system_dependencies(appdir)
    base = "DynamicxEcatTool-v" + release + "-ubuntu" + ubuntu + "-amd64"
    image = publish / (base + ".AppImage")
    run(appimagetool, "--runtime-file", runtime, "--no-appstream", appdir, image,
        env=dict(os.environ, ARCH="x86_64"))
    image.chmod(0o755)

    debroot = destination / "debroot"
    installation = debroot / "opt/dynamicx-ecat-tool"
    shutil.copytree(appdir, installation, symlinks=True)
    (debroot / "usr/bin").mkdir(parents=True)
    executable(debroot / "usr/bin/dynamicx-ecat-tool",
               '#!/bin/sh\nexec /opt/dynamicx-ecat-tool/AppRun "$@"\n')
    desktop_dir = debroot / "usr/share/applications"
    desktop_dir.mkdir(parents=True)
    desktop_text = Path(__file__).with_name("dynamicx-ecat-tool.desktop").read_text()
    (desktop_dir / "dynamicx-ecat-tool.desktop").write_text(desktop_text.replace("Exec=DynamicxEcatTool", "Exec=dynamicx-ecat-tool"))
    icons = debroot / "usr/share/icons/hicolor/scalable/apps"
    icons.mkdir(parents=True)
    shutil.copy2(icon, icons / icon.name)
    control = debroot / "DEBIAN"
    control.mkdir()
    (control / "control").write_text(
        "Package: dynamicx-ecat-tool\nVersion: " + release + "-1ubuntu" + ubuntu + "\n"
        "Architecture: amd64\nMaintainer: WiseL00k <noreply@github.com>\n"
        "Section: utils\nPriority: optional\nDepends: " + dependencies + ", fonts-dejavu-core\n"
        "Recommends: fonts-noto-cjk\nHomepage: https://github.com/WiseL00k/DynamicxEcatTool\n"
        "Description: Qt EtherCAT debugging and testing tool\n"
        " Includes Qt/QML, SOEM and yaml-cpp runtime libraries.\n")
    deb = publish / (base + ".deb")
    run("dpkg-deb", "--build", "--root-owner-group", debroot, deb)
    if output("dpkg-deb", "-f", deb, "Architecture") != "amd64":
        raise ValueError("Invalid deb architecture")
    payload = [{"path": str(p.relative_to(appdir)), "sha256": sha256(p)}
               for p in sorted(appdir.rglob("*")) if p.is_file()]
    manifest = {
        "version": release, "ubuntu": ubuntu, "architecture": "amd64",
        "commit": output("git", "-c", "safe.directory=" + str(ROOT), "-C", ROOT, "rev-parse", "HEAD"),
        "headCommit": os.environ.get("GITHUB_HEAD_SHA"), "runId": os.environ.get("GITHUB_RUN_ID"),
        "qt": DEPENDENCIES["qtVersion"], "compiler": output("g++", "-dumpfullversion"),
        "glibc": output("getconf", "GNU_LIBC_VERSION"), "dependencies": DEPENDENCIES,
        "debDepends": dependencies, "payload": payload,
        "artifacts": [{"name": p.name, "sha256": sha256(p)} for p in (image, deb)]}
    (publish / "build-info.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (publish / "SHA256SUMS").write_text("".join(sha256(p) + "  " + p.name + "\n" for p in (image, deb)))
    if os.environ.get("GITHUB_OUTPUT"):
        with open(os.environ["GITHUB_OUTPUT"], "a") as stream:
            stream.write("version=" + release + "\n")
    print("Packages ready:", publish)


if __name__ == "__main__":
    main()
