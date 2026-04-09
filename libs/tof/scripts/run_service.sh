#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "$ROOT_DIR/run_common.sh"

PYTHON_BIN="$(resolve_tof_python)"
echo "[INFO] Using Python: $PYTHON_BIN"
"$PYTHON_BIN" "$ROOT_DIR/tof_service.py" --config /home/pi/dolydev/config/tof_service.yaml "$@"