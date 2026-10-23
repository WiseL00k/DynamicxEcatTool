"""Shared Linux packaging helpers; compatible with Ubuntu 20.04's Python 3.8."""
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import urllib.request

ROOT = Path(__file__).resolve().parents[2]
DEPENDENCIES = json.loads((Path(__file__).with_name("dependencies.json")).read_text())


def run(*args, **kwargs):
    print("+", " ".join(map(str, args)), flush=True)
    return subprocess.run(list(map(str, args)), check=True, **kwargs)


def output(*args, **kwargs):
    return subprocess.check_output(list(map(str, args)), text=True, **kwargs).strip()


def sha256(path):
    digest = hashlib.sha256()
    with Path(path).open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def version():
    value = (ROOT / "packaging/VERSION").read_text().strip()
    if not re.fullmatch(r"(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)", value):
        raise ValueError("Invalid packaging/VERSION")
    return value


def ubuntu_version():
    info = dict(line.strip().split("=", 1) for line in Path("/etc/os-release").read_text().splitlines() if "=" in line)
    value = info["VERSION_ID"].strip('"')
    if info["ID"].strip('"') != "ubuntu" or value not in ("20.04", "24.04"):
        raise ValueError("Packaging requires Ubuntu 20.04 or 24.04")
    if output("dpkg", "--print-architecture") != "amd64":
        raise ValueError("Only amd64 packages are supported")
    return value


def download(spec, destination):
    destination = Path(destination)
    if not spec["url"].startswith("https://") or not re.fullmatch("[a-f0-9]{64}", spec["sha256"]):
        raise ValueError("Download requires HTTPS and a pinned SHA256")
    if not destination.exists():
        partial = destination.with_suffix(".partial")
        with urllib.request.urlopen(spec["url"], timeout=180) as source, partial.open("wb") as target:
            while True:
                chunk = source.read(1024 * 1024)
                if not chunk:
                    break
                target.write(chunk)
        if sha256(partial) != spec["sha256"]:
            partial.unlink()
            raise ValueError("SHA256 mismatch: " + spec["url"])
        partial.rename(destination)
    if sha256(destination) != spec["sha256"]:
        raise ValueError("SHA256 mismatch: " + str(destination))
    return destination


def checkout(repository, commit, destination):
    run("git", "init", destination)
    run("git", "-C", destination, "fetch", "--depth=1", repository, commit)
    run("git", "-C", destination, "checkout", "--detach", "FETCH_HEAD")
    if output("git", "-C", destination, "rev-parse", "HEAD") != commit:
        raise ValueError("Dependency commit mismatch")
