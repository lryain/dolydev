#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DRIVE_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
SDK_INCLUDE_DIR="/home/pi/DOLY-DIY/SDK/include"
SDK_LIB_DIR="/home/pi/DOLY-DIY/SDK/lib"
LOCAL_INCLUDE_DIR="${DRIVE_DIR}/include/sdk"
LOCAL_LIB_DIR="/home/pi/dolydev/libs/Doly/libs"
SYSTEM_SPDLOG_LIB="/usr/local/lib/libspdlog.so.1.17"

headers=(
    DriveControl.h
    DriveEvent.h
    DriveEventListener.h
    Helper.h
)

sdk_libs=(
    libDriveControl.so
    libImuControl.so
    libMotorControl.so
    libGpio.so
    libTimer.so
    libHelper.so
    libColor.so
    libTinyXml.so
)

echo "[sync] preparing local SDK mirror directories"
mkdir -p "${LOCAL_INCLUDE_DIR}" "${LOCAL_LIB_DIR}"

echo "[sync] copying DriveControl public headers"
for header in "${headers[@]}"; do
    src="${SDK_INCLUDE_DIR}/${header}"
    dst="${LOCAL_INCLUDE_DIR}/${header}"
    if [[ ! -f "${src}" ]]; then
        echo "[sync][error] missing SDK header: ${src}" >&2
        exit 1
    fi
    install -m 644 "${src}" "${dst}"
done

echo "[sync] copying DriveControl runtime libraries"
for lib in "${sdk_libs[@]}"; do
    src="${SDK_LIB_DIR}/${lib}"
    dst="${LOCAL_LIB_DIR}/${lib}"
    if [[ ! -f "${src}" ]]; then
        echo "[sync][error] missing SDK library: ${src}" >&2
        exit 1
    fi
    install -m 755 "${src}" "${dst}"
done

if [[ -f "${SYSTEM_SPDLOG_LIB}" ]]; then
    echo "[sync] copying runtime dependency libspdlog.so.1.17"
    install -m 755 "${SYSTEM_SPDLOG_LIB}" "${LOCAL_LIB_DIR}/libspdlog.so.1.17"
    ln -sf "libspdlog.so.1.17" "${LOCAL_LIB_DIR}/libspdlog.so"
else
    echo "[sync][warn] ${SYSTEM_SPDLOG_LIB} not found; keep relying on /usr/local/lib at runtime" >&2
fi

echo "[sync] DriveControl SDK mirror refreshed"