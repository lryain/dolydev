#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
SDK_ROOT="${SDK_ROOT:-/home/pi/DOLY-DIY/SDK}"

make -C "${ROOT_DIR}" -j2
cmake -S "${SCRIPT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug -DSDK_ROOT="${SDK_ROOT}"
cmake --build "${BUILD_DIR}" -j2
