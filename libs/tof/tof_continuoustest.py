#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Continuous VL6180X range reader with safe cleanup on exit."""

import argparse
import logging
import os
import signal
import time

from adafruit_extended_bus import ExtendedI2C as I2C
import adafruit_vl6180x

from tof_common import (
    TOF_ADDR_LEFT as ADDR_LEFT,
    TOF_ADDR_RIGHT as ADDR_RIGHT,
    TOF_ENL_PIN,
    TOF_I2C_BUS as I2C_BUS,
    TOF_SETUP_MARKER as SETUP_MARKER,
    close_i2c_bus,
    load_offsets,
    tof_en_ctl_path,
    tof_offsets_json_path,
    setup_sensors,
    stop_range_continuous,
)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EN_CTL = tof_en_ctl_path(SCRIPT_DIR)
LOG_DIR = os.path.join(os.path.dirname(os.path.dirname(SCRIPT_DIR)), 'logs')

os.makedirs(LOG_DIR, exist_ok=True)
logging.basicConfig(
    filename=os.path.join(LOG_DIR, f"tof_continuoustest_{int(time.time())}.log"),
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
)

OFFSET_JSON = tof_offsets_json_path(SCRIPT_DIR)


def handle_exit_signal(signum, _frame):
    logging.info('received exit signal %s', signum)
    raise SystemExit(128 + signum)


def main() -> None:
    parser = argparse.ArgumentParser(description='Continuous VL6180X range reader')
    parser.add_argument('--skip-setup', action='store_true', help='skip setup_sensors if marker present')
    parser.add_argument('--force-setup', action='store_true', help='force running setup_sensors (remove marker)')
    args = parser.parse_args()

    signal.signal(signal.SIGTERM, handle_exit_signal)
    signal.signal(signal.SIGHUP, handle_exit_signal)

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

    offsets = load_offsets(OFFSET_JSON, logging)
    i2c = None
    left = None
    right = None

    try:
        i2c = I2C(I2C_BUS)
        left = adafruit_vl6180x.VL6180X(i2c, address=ADDR_LEFT, offset=offsets['left'])
        right = adafruit_vl6180x.VL6180X(i2c, address=ADDR_RIGHT, offset=offsets['right'])

        print(f'当前左侧偏移量: {left.offset} mm, 右侧偏移量: {right.offset} mm')
        print('TOF demo running. Ctrl-C to exit.')
        print('Starting continuous mode')
        left.start_range_continuous(20)
        right.start_range_continuous(20)

        while True:
            print(f'RangeL: {left.range}mm | RangeR: {right.range}mm')
            time.sleep(0.01)
    except KeyboardInterrupt:
        logging.info('keyboard interrupt received, stopping continuous mode')
    finally:
        stop_range_continuous(left, 'left', logging)
        stop_range_continuous(right, 'right', logging)
        close_i2c_bus(i2c, logging)


if __name__ == '__main__':
    main()