#!/usr/bin/env bash
set -euo pipefail
export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
  build-essential git ca-certificates python3 python3-venv python3-pip \
  pkg-config patchelf file desktop-file-utils zstd xz-utils \
  libgl1-mesa-dev libegl1-mesa-dev libfontconfig1-dev libfreetype6-dev \
  libx11-dev libx11-xcb-dev libxext-dev libxfixes-dev libxi-dev libxrender-dev \
  libxcb1-dev libxcb-cursor-dev libxcb-glx0-dev libxcb-icccm4-dev \
  libxcb-image0-dev libxcb-keysyms1-dev libxcb-randr0-dev libxcb-render-util0-dev \
  libxcb-shape0-dev libxcb-shm0-dev libxcb-sync-dev libxcb-xfixes0-dev \
  libxcb-xinerama0-dev libxcb-xkb-dev libxkbcommon-dev libxkbcommon-x11-dev \
  libdbus-1-dev libssl-dev libpng-dev libjpeg-dev zlib1g-dev \
  fonts-dejavu-core fonts-noto-cjk
python3 -m venv /opt/dynamicx-tools
/opt/dynamicx-tools/bin/pip install --disable-pip-version-check cmake==3.31.6 ninja==1.11.1.4
echo /opt/dynamicx-tools/bin >> "$GITHUB_PATH"
