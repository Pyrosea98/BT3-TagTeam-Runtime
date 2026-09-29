#!/usr/bin/env bash
# Build a **portable** Linux release against glibc 2.35 (Ubuntu 22.04), so the packaged
# the runner runs on Ubuntu/Kubuntu 22.04 and 24.04 (glibc 2.35 / 2.39) and other
# distros with glibc >= 2.35.
#
# Why: a native build inherits the build host's glibc. Building on a rolling distro (e.g. Arch,
# glibc 2.44) produces a binary that needs GLIBC_2.4x and aborts on 24.04 with:
#     libm.so.6: version `GLIBC_2.43' not found (required by the runner)
# Self-builds on the user's own distro are unaffected (they use their own glibc); this wrapper is
# only for producing a distributable artifact.
#
# Requires a container engine (docker, or podman via CONTAINER_ENGINE). Pass the ISO and any
# games/bt3/setup.py flag through:
#     scripts/build-linux-portable.sh /path/to/bt3-usa.iso --jobs 8
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENGINE="${CONTAINER_ENGINE:-docker}"
BASE_IMAGE="${BT3_PORTABLE_BASE:-ubuntu:22.04}"
FLOOR="${BT3_GLIBC_MAX:-2.35}"

if ! command -v "$ENGINE" >/dev/null 2>&1; then
    echo "error: '$ENGINE' not found (set CONTAINER_ENGINE=podman to use podman)" >&2
    exit 1
fi

# Mount any ISO read-only and rewrite its path for inside the container.
MOUNTS=(); ARGS=()
for a in "$@"; do
    if [[ "$a" == *.iso || "$a" == *.ISO ]] && [[ -f "$a" ]]; then
        abs="$(readlink -f "$a")"
        MOUNTS+=(-v "$abs":/iso/bt3.iso:ro)
        ARGS+=("/iso/bt3.iso")
    else
        ARGS+=("$a")
    fi
done

echo "[portable] $BASE_IMAGE, glibc floor $FLOOR, engine $ENGINE"
"$ENGINE" run --rm \
    -v "$ROOT":/src -w /src "${MOUNTS[@]}" \
    -e "BT3_GLIBC_MAX=$FLOOR" \
    "$BASE_IMAGE" bash -lc '
        set -euo pipefail
        export DEBIAN_FRONTEND=noninteractive
        apt-get update
        apt-get install -y --no-install-recommends \
            python3 ca-certificates clang g++ cmake ninja-build pkg-config git bash file \
            libx11-dev libxrandr-dev libxi-dev libxcursor-dev libxinerama-dev \
            libgl1-mesa-dev libglu1-mesa-dev libarchive-tools libarchive-dev p7zip-full \
            libavcodec-dev libavformat-dev libavutil-dev libswresample-dev libswscale-dev \
            ccache mold
        # The repo is owned by the host user but we run as root in the container; without this,
        # git refuses to operate ("dubious ownership") and the parallel-gs submodule is skipped.
        git config --global --add safe.directory /src
        # Build with the distro GCC, the same compiler family as the native dev builds. The clang
        # this package list pulls in on 22.04 is clang-14, which lacks __builtin_source_location, so
        # libstdc++-12 does not define std::source_location and the bundled toml11 fails to compile.
        export CC=gcc CXX=g++
        # Use a fresh build dir: any existing repo build/ has a CMakeCache.txt recorded with the
        # host path, which CMake rejects when the tree is mounted at /src inside the container.
        export PS2X_BUILD_DIR="${PS2X_BUILD_DIR:-/src/build/portable}"
        python3 games/bt3/setup.py "$@" --package
    ' _ "${ARGS[@]}"

echo "[portable] done — the release tree/archive is under games/bt3/work/ (glibc floor $FLOOR)"
