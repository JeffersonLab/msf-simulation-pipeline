#!/usr/bin/env bash
# build_macros.sh — compiles the csv_convert macros as executables for the parity harness.
#
# Run inside a container that ships podio, EDM4HEP and EDM4EIC. The macro directory's
# CMakeLists globs every .cxx; two macros (comb_candidates, lambda_acceptance) have no
# main() and fail to link, so this script builds named targets only.
#
# Usage: tests/build_macros.sh [build-dir] [target ...]
#   default build dir: ../csv_convert/build
#   default targets: the three PHASE 1 roles
set -euo pipefail

macro_dir="$(cd "$(dirname "$0")/../../csv_convert" && pwd)"
build_dir="${1:-$macro_dir/build}"
shift || true
targets=("$@")
if [ ${#targets[@]} -eq 0 ]; then
    targets=(edm4eic_trk_hits edm4eic_cal_hits edm4eic_calo_clusters)
fi

cmake -S "$macro_dir" -B "$build_dir" -DCMAKE_BUILD_TYPE=Release
cmake --build "$build_dir" -j "$(( $(nproc) > 2 ? $(nproc) - 2 : 1 ))" --target "${targets[@]}"
