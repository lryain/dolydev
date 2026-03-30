#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Calibrate paired VL6180X sensors and persist offsets."""

import argparse
import json
import logging
import os
import time
from pathlib import Path

from adafruit_extended_bus import ExtendedI2C as I2C
import adafruit_vl6180x

from tof_common import (
    TOF_ADDR_LEFT as ADDR_LEFT,
    TOF_ADDR_RIGHT as ADDR_RIGHT,
    TOF_ENL_PIN,
    TOF_I2C_BUS as I2C_BUS,
    TOF_SETUP_MARKER as SETUP_MARKER,
    load_offsets,
    tof_en_ctl_path,
    tof_offsets_json_path,
    setup_sensors,
)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EN_CTL = tof_en_ctl_path(SCRIPT_DIR)
LOG_DIR = os.path.join(os.path.dirname(os.path.dirname(SCRIPT_DIR)), 'logs')

os.makedirs(LOG_DIR, exist_ok=True)
logging.basicConfig(
    filename=os.path.join(LOG_DIR, f"tof_calibration_{int(time.time())}.log"),
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
)

CALIBRATION_MARKER = Path('/tmp/tof_calibration_setup_done')
OFFSET_STORE = tof_offsets_json_path(SCRIPT_DIR)
LEFT_TARGET_MM = 120.0
RIGHT_TARGET_MM = 120.0
SAMPLES_PER_SENSOR = 32
SLEEP_BETWEEN_SECS = 0.05


def clamp_offset(value: float) -> int:
    return max(-128, min(127, int(round(value))))


def collect_average(sensor, label: str, samples: int = SAMPLES_PER_SENSOR) -> float:
    sensor.offset = 0
    values = []
    for _ in range(samples):
        value = sensor.range
        values.append(value)
        time.sleep(SLEEP_BETWEEN_SECS)
    average = sum(values) / len(values)
    print(f'{label} average over {samples} samples: {average:.2f} mm')
    logging.info('collect_average %s -> %.2f', label, average)
    return average


def save_offsets(left_offset: int, right_offset: int) -> None:
    payload = {'left': left_offset, 'right': right_offset}
    OFFSET_STORE.write_text(json.dumps(payload))
    logging.info('Offsets saved: %s', payload)


def apply_saved_offsets(left_sensor, right_sensor) -> bool:
    if not OFFSET_STORE.exists():
        return False
    try:
        data = json.loads(OFFSET_STORE.read_text())
        left_value = int(data.get('left', 0))
        right_value = int(data.get('right', 0))
        left_sensor.offset = clamp_offset(left_value)
        right_sensor.offset = clamp_offset(right_value)
        print(f'Applied stored offsets: left={left_value}, right={right_value}')
        logging.info('Applied saved offsets L=%d R=%d', left_value, right_value)
        return True
    except Exception as exc:
        logging.warning('could not apply saved offsets: %s', exc)
        return False


def mark_calibrated(left_offset: int, right_offset: int) -> None:
    try:
        CALIBRATION_MARKER.write_text(f'{int(time.time())} left={left_offset} right={right_offset}\n')
        logging.info('Calibration marker written, offsets left=%d right=%d', left_offset, right_offset)
    except Exception as exc:
        logging.warning('failed to write calibration marker: %s', exc)


def main() -> None:
    parser = argparse.ArgumentParser(description='VL6180X calibration helper')
    parser.add_argument('--skip-setup', action='store_true', help='skip setup_sensors if marker present')
    parser.add_argument('--force-setup', action='store_true', help='force running setup_sensors (remove marker)')
    parser.add_argument('--force-calibration', action='store_true', help='force running calibration even if marker exists')
    args = parser.parse_args()

    if args.force_setup and SETUP_MARKER.exists():
        SETUP_MARKER.unlink()
        logging.info('force_setup: removed marker %s', SETUP_MARKER)

    if not args.skip_setup:
        setup_sensors(
            i2c_bus=I2C_BUS,
            addr_left=ADDR_LEFT,
            addr_right=ADDR_RIGHT,
            en_ctl=EN_CTL,
            tof_en_pin=TOF_ENL_PIN,
            setup_marker=SETUP_MARKER,
            logger=logging,
        )

    i2c = I2C(I2C_BUS)
    left_tof = adafruit_vl6180x.VL6180X(i2c, address=ADDR_LEFT, offset=0)
    right_tof = adafruit_vl6180x.VL6180X(i2c, address=ADDR_RIGHT, offset=0)

    applied_offsets = apply_saved_offsets(left_tof, right_tof)
    if CALIBRATION_MARKER.exists() and not args.force_calibration:
        print(f'calibrate_sensors: marker {CALIBRATION_MARKER} exists; skipping calibration')
        logging.info('calibrate_sensors: marker exists, skipping (applied=%s)', applied_offsets)
        return

    if CALIBRATION_MARKER.exists() and args.force_calibration:
        logging.info('force_calibration: marker present but continuing to recalibrate (applied=%s)', applied_offsets)

    print(f'当前左侧偏移量: {left_tof.offset} mm, 右侧偏移量: {right_tof.offset} mm')
    print('请将左侧传感器前方放置一个距离135mm 的目标物，按回车键继续...')
    input()
    avg_left = collect_average(left_tof, '左侧')
    left_offset = clamp_offset(LEFT_TARGET_MM - avg_left)
    print(f'Left TOF calibration offset to apply: {left_offset} mm (clamped)')
    left_tof.offset = left_offset

    print('请将右侧传感器前方放置 50mm 目标，按回车键继续...')
    input()
    avg_right = collect_average(right_tof, '右侧')
    right_offset = clamp_offset(RIGHT_TARGET_MM - avg_right)
    print(f'Right TOF calibration offset to apply: {right_offset} mm (clamped)')
    right_tof.offset = right_offset

    save_offsets(left_offset, right_offset)
    mark_calibrated(left_offset, right_offset)


if __name__ == '__main__':
    main()