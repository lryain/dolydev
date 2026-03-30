#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Shared helpers for TOF test scripts."""

from __future__ import annotations

import json
import logging
import subprocess
import time
from pathlib import Path
from typing import Any

TOF_I2C_BUS = 6
TOF_ADDR_LEFT = 0x29
TOF_ADDR_RIGHT = 0x30
TOF_ENL_PIN = 64
TOF_SETUP_MARKER = Path('/tmp/tof_demo_setup_done')
TOF_LUX_CALIBRATION_MARKER = Path('/tmp/tof_lux_calibration_done')


def tof_en_ctl_path(script_dir: str | Path) -> str:
    return str(Path(script_dir) / 'build' / 'tof_en_ctl')


def tof_offsets_json_path(script_dir: str | Path) -> Path:
    return Path(script_dir) / 'data' / 'tof_offsets.json'


def tof_lux_calibration_json_path(script_dir: str | Path) -> Path:
    return Path(script_dir) / 'data' / 'tof_lux_calibration.json'


def check_i2c_addresses(
    i2c_bus: int,
    addr_left: int,
    addr_right: int,
    logger: logging.Logger | None = None,
) -> bool:
    """Return True if both TOF addresses are visible on the I2C bus."""
    addr_left_hex = format(addr_left, '02x')
    addr_right_hex = format(addr_right, '02x')
    try:
        out = subprocess.check_output(['i2cdetect', '-y', str(i2c_bus)], stderr=subprocess.STDOUT)
        tokens = out.decode(errors='ignore').lower().split()
        present_left = addr_left_hex in tokens
        present_right = addr_right_hex in tokens
        if logger is not None:
            logger.info("i2cdetect output check: left=%s right=%s", present_left, present_right)
        return present_left and present_right
    except Exception as exc:
        if logger is not None:
            logger.warning("failed to run i2cdetect for check: %s", exc)
        return False

def run_en(en_ctl: str, pin: int, cmd: str, logger: logging.Logger | None = None) -> None:
    try:
        subprocess.check_call([en_ctl, cmd, "--pin", str(pin)])
    except Exception as exc:
        if logger is not None:
            logger.error("en_ctl %s failed: %s", cmd, exc)
        print("[ERR] en_ctl", cmd, "failed:", exc)
        raise SystemExit(2) from exc


def load_offsets(offset_json: Path, logger: logging.Logger | None = None) -> dict[str, int]:
    if not offset_json.exists():
        if logger is not None:
            logger.warning("offset file %s missing", offset_json)
        return {"left": 0, "right": 0}

    try:
        data = json.loads(offset_json.read_text())
        return {
            "left": int(data.get("left", 0)),
            "right": int(data.get("right", 0)),
        }
    except Exception as exc:
        if logger is not None:
            logger.warning("failed to read offsets: %s", exc)
        return {"left": 0, "right": 0}


def stop_range_continuous(
    sensor: Any,
    name: str = "sensor",
    logger: logging.Logger | None = None,
    settle_seconds: float = 0.3,
) -> None:
    if sensor is None:
        return

    try:
        sensor.stop_range_continuous()
        time.sleep(settle_seconds)
        if logger is not None:
            logger.info("stopped continuous mode for %s", name)
    except Exception as exc:
        if logger is not None:
            logger.warning("failed to stop %s cleanly: %s", name, exc)


def setup_sensors(
    *,
    i2c_bus: int,
    addr_left: int,
    addr_right: int,
    en_ctl: str,
    tof_en_pin: int,
    setup_marker: Path | None = None,
    auto_confirm: bool = False,
    logger: logging.Logger | None = None,
) -> bool:
    """Configure TOF addresses and skip if they are already present.

    Returns True when setup work was performed, False when it was skipped.
    """
    if check_i2c_addresses(i2c_bus, addr_left, addr_right, logger):
        print(f"setup_sensors: detected 0x{addr_left:02x} and 0x{addr_right:02x} already present; skipping setup")
        if logger is not None:
            logger.info("setup_sensors: addresses already present, skipping setup")
        return False

    run_en(en_ctl, tof_en_pin, 'off', logger)
    time.sleep(0.02)
    print(f"先关闭左侧 run_en('off'), 此时执行 i2cdetect -y {i2c_bus} 应检测到 0x{addr_left:02x}")
    subprocess.check_call(['i2cdetect', '-y', str(i2c_bus)])

    from adafruit_extended_bus import ExtendedI2C as I2C
    import adafruit_vl6180x

    i2c = I2C(i2c_bus)
    sensor = adafruit_vl6180x.VL6180X(i2c, address=addr_left)

    if auto_confirm:
        confirm = 'y'
    else:
        confirm = input(
            f"即将更改右侧传感器地址从 0x{addr_left:02x} 到 0x{addr_right:02x}，确认？(y/n): "
        ).strip().lower()
    if confirm != 'y':
        print("地址更改已取消。")
        raise SystemExit(1)

    sensor._write_8(0x212, addr_right)
    time.sleep(0.02)
    print(f"已将右侧改到 0x{addr_right:02x}, 再次执行 i2cdetect -y {i2c_bus} 应检测到 0x{addr_right:02x}")
    subprocess.check_call(['i2cdetect', '-y', str(i2c_bus)])

    run_en(en_ctl, tof_en_pin, 'on', logger)
    time.sleep(0.05)
    print(
        f"打开左侧，保持 0x{addr_left:02x}, 此时执行 i2cdetect -y {i2c_bus} 应检测到 0x{addr_left:02x} 0x{addr_right:02x}"
    )
    subprocess.check_call(['i2cdetect', '-y', str(i2c_bus)])

    if setup_marker is not None:
        try:
            setup_marker.write_text(f"done {int(time.time())}\n")
            if logger is not None:
                logger.info("setup_sensors: wrote marker %s", setup_marker)
        except Exception as exc:
            if logger is not None:
                logger.warning("failed to write setup marker: %s", exc)

    return True

def close_i2c_bus(i2c: Any, logger: logging.Logger | None = None) -> None:
    if i2c is None:
        return

    for method_name in ("deinit", "close"):
        method = getattr(i2c, method_name, None)
        if callable(method):
            try:
                method()
                if logger is not None:
                    logger.info("closed i2c bus via %s()", method_name)
            except Exception as exc:
                if logger is not None:
                    logger.warning("failed to close i2c bus via %s(): %s", method_name, exc)
            return