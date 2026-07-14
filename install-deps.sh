#!/usr/bin/env bash
# Install kilix-amp build/runtime dependencies on Debian/Ubuntu systems.
set -euo pipefail

if [ "$(id -u)" -ne 0 ]; then
    exec sudo "$0" "$@"
fi

export DEBIAN_FRONTEND=noninteractive
apt-get update
apt-get install -y --no-install-recommends \
    build-essential \
    pkg-config \
    libsdl2-dev \
    libsdl2-image-dev \
    libsndfile1-dev \
    zlib1g-dev \
    libfluidsynth-dev \
    fluidsynth \
    fluid-soundfont-gm \
    zenity
