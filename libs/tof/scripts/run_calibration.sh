#!/usr/bin/env bash
set -euo pipefail
source "$(dirname "$0")/run_common.sh"
run_tof_script tof_calibration.py "$@"
