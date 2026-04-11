#!/usr/bin/env bash
set -euo pipefail

TOF_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

ensure_tof_en_ctl() {
	local bin="$TOF_DIR/build/tof_en_ctl"
	if [[ -x "$bin" ]]; then
		return 0
	fi

	echo "[INFO] en_ctl not built. Attempting to build by running build_tof_enctl.sh..."
	if bash "$TOF_DIR/build_tof_enctl.sh"; then
		if [[ ! -x "$bin" ]]; then
			echo "[ERR] Build succeeded but $bin still missing" >&2
			return 1
		fi
		echo "Built: $bin"
	else
		echo "[ERR] build_tof_enctl.sh failed. Please run it manually to see errors." >&2
		return 1
	fi
}

resolve_tof_python() {
	local preferred=(
		"${PY:-}"
		"/home/pi/dolydev/.venv/bin/python"
		"/home/pi/dolydev/.venv/bin/python"
		"python3"
	)
	local py
	for py in "${preferred[@]}"; do
		if [[ -z "$py" ]]; then
			continue
		fi
		if [[ "$py" != "python3" && ! -x "$py" ]]; then
			continue
		fi
		if "$py" -c "import yaml, zmq" >/dev/null 2>&1; then
			if "$py" -c "import adafruit_extended_bus, adafruit_vl6180x" >/dev/null 2>&1; then
				printf '%s\n' "$py"
				return 0
			fi
		fi
	done
	printf '%s\n' "python3"
}

run_tof_script() {
	local script="$1"
	shift || true

	ensure_tof_en_ctl
	local py
	py="$(resolve_tof_python)"

	"$py" "$TOF_DIR/$script" "$@"
}