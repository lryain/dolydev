#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
SDK_ROOT="${SDK_ROOT:-/home/pi/DOLY-DIY/SDK}"

if [[ $# -lt 1 ]]; then
    echo "usage: $0 {demo|simpletest|continuoustest|calibration|alts_cal|cruise|smoke|smoke-left|smoke-paired} [args...]" >&2
    exit 2
fi

TARGET="$1"
shift

if [[ ! -x "${BUILD_DIR}/tof_demo" ]]; then
    "${SCRIPT_DIR}/build.sh"
fi

export LD_LIBRARY_PATH="${ROOT_DIR}:${SDK_ROOT}/lib:/home/pi/dolydev/libs/Doly/libs:/.doly/libs/opencv/lib:/usr/local/lib:${LD_LIBRARY_PATH:-}"

case "${TARGET}" in
    demo)
        exec "${BUILD_DIR}/tof_demo" "$@"
        ;;
    simpletest)
        exec "${BUILD_DIR}/tof_simpletest" "$@"
        ;;
    continuoustest)
        exec "${BUILD_DIR}/tof_continuoustest" "$@"
        ;;
    calibration)
        exec "${BUILD_DIR}/tof_calibration" "$@"
        ;;
    alts_cal)
        exec "${BUILD_DIR}/tof_alts_cal" "$@"
        ;;
    cruise)
        exec "${BUILD_DIR}/tof_cruise" "$@"
        ;;
    historytest)
        exec "${BUILD_DIR}/tof_historytest" "$@"
        ;;
    perform)
        exec "${BUILD_DIR}/tof_perform" "$@"
        ;;
    smoke)
        "${BUILD_DIR}/tof_demo" --skip-setup --samples 5 --interval 0.05 "$@"
        "${BUILD_DIR}/tof_simpletest" --skip-setup --samples 5 --interval 0.05
        "${BUILD_DIR}/tof_continuoustest" --skip-setup --samples 20 --period-ms 20 --interval 0.01
        "${BUILD_DIR}/tof_alts_cal" --skip-setup --skip-calibration --samples 5 --interval 0.2
        ;;
    smoke-left)
        "${BUILD_DIR}/tof_demo" --skip-setup --side left --samples 5 --interval 0.05 "$@"
        "${BUILD_DIR}/tof_simpletest" --skip-setup --side left --samples 5 --interval 0.05
        "${BUILD_DIR}/tof_continuoustest" --skip-setup --side left --samples 20 --period-ms 20 --interval 0.01
        ;;
    smoke-paired)
        "${BUILD_DIR}/tof_demo" --skip-setup --samples 5 --interval 0.05 "$@"
        "${BUILD_DIR}/tof_simpletest" --skip-setup --samples 5 --interval 0.05
        "${BUILD_DIR}/tof_continuoustest" --skip-setup --samples 20 --period-ms 20 --interval 0.01
        "${BUILD_DIR}/tof_alts_cal" --skip-setup --skip-calibration --samples 5 --interval 0.2
        ;;
    *)
        echo "unknown target: ${TARGET}" >&2
        exit 2
        ;;
esac
