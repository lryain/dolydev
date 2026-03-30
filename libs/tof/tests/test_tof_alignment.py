#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Rotate Doly and read paired VL6180X distances directly via the local cpp-backed
Python wrapper until both sensors are around 120mm (+/- 5mm).
"""

from __future__ import annotations

import sys
import time
from pathlib import Path

TOF_ROOT = Path(__file__).resolve().parents[1]
if str(TOF_ROOT) not in sys.path:
    sys.path.insert(0, str(TOF_ROOT))

SDK_BUILD_DIRS = [
    Path("/home/pi/DOLY-DIY/SDK/examples/python/DriveControl/build"),
    Path("/home/pi/DOLY-DIY/SDK/examples/python/Helper/build"),
]

for sdk_build_dir in SDK_BUILD_DIRS:
    sdk_path = str(sdk_build_dir)
    if sdk_build_dir.exists() and sdk_path not in sys.path:
        sys.path.insert(0, sdk_path)

from adafruit_extended_bus import ExtendedI2C as I2C
import adafruit_vl6180x

import doly_drive as drive  # type: ignore[import-not-found]
import doly_helper as helper  # type: ignore[import-not-found]
from tof_common import (
    TOF_ADDR_LEFT as ADDR_LEFT,
    TOF_ADDR_RIGHT as ADDR_RIGHT,
    TOF_ENL_PIN,
    TOF_I2C_BUS as I2C_BUS,
    TOF_SETUP_MARKER,
    close_i2c_bus,
    load_offsets,
    setup_sensors,
    stop_range_continuous,
    tof_en_ctl_path,
)

TARGET_DISTANCE_MM = 120
TOLERANCE_MM = 5
ROTATE_SPEED = 25
ROTATE_STEP_DEG = 2.0
FORWARD_STEP_MM = 20
MAX_ITERATIONS = 120


def main() -> int:
    script_dir = Path(__file__).resolve().parents[1]
    en_ctl = tof_en_ctl_path(script_dir)

    print("=== Doly TOF Alignment Test (direct cpp-backed sensor read) ===")

    if helper.stop_doly_service() < 0:
        print("[warn] Failed to stop doly service; continuing if it is already stopped")

    setup_sensors(
        i2c_bus=I2C_BUS,
        addr_left=ADDR_LEFT,
        addr_right=ADDR_RIGHT,
        en_ctl=en_ctl,
        tof_en_pin=TOF_ENL_PIN,
        setup_marker=TOF_SETUP_MARKER,
    )

    rc = drive.init(0, 0, 0, 0, 0, 0)
    if rc != 0:
        print(f"[error] DriveControl init failed with code: {rc}")
        return 1

    i2c = None
    left = None
    right = None

    try:
        offsets = load_offsets(script_dir / "data" / "tof_offsets.json")
        i2c = I2C(I2C_BUS)
        left = adafruit_vl6180x.VL6180X(i2c, address=ADDR_LEFT, offset=offsets["left"])
        right = adafruit_vl6180x.VL6180X(i2c, address=ADDR_RIGHT, offset=offsets["right"])
        left.start_range_continuous(20)
        right.start_range_continuous(20)

        print(f"[info] goal={TARGET_DISTANCE_MM}mm +/- {TOLERANCE_MM}mm")
        for iteration in range(1, MAX_ITERATIONS + 1):
            left_mm = left.range
            right_mm = right.range
            balance_error = left_mm - right_mm
            print(f"[info] iter={iteration:03d} left={left_mm}mm right={right_mm}mm balance={balance_error}mm")

            if abs(left_mm - TARGET_DISTANCE_MM) <= TOLERANCE_MM and abs(right_mm - TARGET_DISTANCE_MM) <= TOLERANCE_MM:
                print(f"[success] reached target window: left={left_mm} right={right_mm}")
                return 0

            if abs(balance_error) > TOLERANCE_MM:
                direction = -1.0 if balance_error > 0 else 1.0
                drive.go_rotate(100 + iteration, direction * ROTATE_STEP_DEG, True, ROTATE_SPEED, True, True)
            else:
                if left_mm > TARGET_DISTANCE_MM + TOLERANCE_MM:
                    drive.go_distance(101 + iteration, FORWARD_STEP_MM, ROTATE_SPEED, True, True, 0, False, True)
                elif left_mm < TARGET_DISTANCE_MM - TOLERANCE_MM:
                    drive.go_distance(101 + iteration, FORWARD_STEP_MM, ROTATE_SPEED, False, True, 0, False, True)
                else:
                    print(f"[success] aligned at target window: left={left_mm} right={right_mm}")
                    return 0

            while drive.get_state() == drive.DriveState.Running:
                time.sleep(0.05)
            time.sleep(0.2)

        print("[error] max iterations reached without hitting target window")
        return 2
    except KeyboardInterrupt:
        print("[info] interrupted by user")
        return 130
    finally:
        stop_range_continuous(left, "left")
        stop_range_continuous(right, "right")
        close_i2c_bus(i2c)
        drive.dispose(True)
        print("=== Test Finished ===")


if __name__ == "__main__":
    raise SystemExit(main())
