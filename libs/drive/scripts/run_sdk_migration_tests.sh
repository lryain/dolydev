#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRIVE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${DRIVE_DIR}/build"
LOCAL_LIB_DIR="/home/pi/dolydev/libs/Doly/libs"

bash "${SCRIPT_DIR}/sync_drivecontrol_sdk.sh"

mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=OFF
make -j2 test_drive_sdk_bridge

export LD_LIBRARY_PATH="${LOCAL_LIB_DIR}:/usr/local/lib:${LD_LIBRARY_PATH:-}"

pkill -f drive_service >/dev/null 2>&1 || true

./test_drive_sdk_bridge --distance-mm 20 --angle-deg 12 --speed 15 --timeout-ms 5000