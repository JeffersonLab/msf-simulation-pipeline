#!/usr/bin/env bash
# build_in_image.sh — configures and builds eic2ai inside one of the local EIC docker images.
#
# Usage: tests/build_in_image.sh <image> <build-dir-name> [jobs]
#   e.g.  tests/build_in_image.sh eicweb/eic_xl:nightly build-eic_xl-nightly 6
#
# The repository root is mounted at /work and /data at /data; the build runs as the calling
# user so the build directory stays owned by you. minizip-ng is downloaded at configure time
# (network needed once per build directory).
set -euo pipefail

image=$1
build_dir=$2
jobs=${3:-6}
repo_root="$(cd "$(dirname "$0")/../../../.." && pwd)"

docker run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp \
    -v "$repo_root:/work" -v /data:/data "$image" bash -c "
    set -e
    cd /work/submodules/simulation-pipeline/eic2ai
    cmake -S . -B '$build_dir' -DCMAKE_BUILD_TYPE=Release
    cmake --build '$build_dir' -j '$jobs'
    ./'$build_dir'/eic2ai --list-writers"
