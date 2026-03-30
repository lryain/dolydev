#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""VL6180X performance benchmark for single-shot, continuous, and history reads."""

import argparse
import logging
import os
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
    filename=os.path.join(LOG_DIR, f"tof_perform_{int(time.time())}.log"),
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
)

OFFSET_JSON = tof_offsets_json_path(SCRIPT_DIR)


def main() -> None:
    parser = argparse.ArgumentParser(description='VL6180X performance benchmark')
    parser.add_argument('--skip-setup', action='store_true', help='skip setup_sensors if marker present')
    parser.add_argument('--force-setup', action='store_true', help='force running setup_sensors (remove marker)')
    parser.add_argument('--measurements', type=int, default=100, help='number of measurements per phase')
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

    offsets = load_offsets(OFFSET_JSON, logging)
    i2c = None
    left_tof = None
    right_tof = None

    try:
        i2c = I2C(I2C_BUS)
        left_tof = adafruit_vl6180x.VL6180X(i2c, address=ADDR_LEFT, offset=offsets['left'])
        right_tof = adafruit_vl6180x.VL6180X(i2c, address=ADDR_RIGHT, offset=offsets['right'])

        print(f'当前左侧偏移量: {left_tof.offset} mm, 右侧偏移量: {right_tof.offset} mm')
        print('TOF demo running. Ctrl-C to exit.')

        print('Starting single-shot measurement...')
        start = time.time()
        for _ in range(args.measurements):
            left_tof.range
            right_tof.range
        print(f'Performed {args.measurements} measurements in single-shot mode in {time.time() - start}s\n')

        time.sleep(2)

        print('Starting continuous measurement...')
        left_tof.start_range_continuous(20)
        right_tof.start_range_continuous(20)
        start = time.time()
        for _ in range(args.measurements):
            left_tof.range
            right_tof.range
        print(f'Performed {args.measurements} measurements in continuous mode in {time.time() - start}s\n')

        print('Starting continuous measurement with history enabled...')
        start = time.time()
        for _ in range(args.measurements):
            left_tof.range_from_history
            right_tof.range_from_history
        print(f'Performed {args.measurements} measurements in continuous mode, reading from history, in {time.time() - start}s\n')
        print('Finished')
    except KeyboardInterrupt:
        logging.info('keyboard interrupt received, stopping performance test')
    finally:
        stop_range_continuous(left_tof, 'left', logging)
        stop_range_continuous(right_tof, 'right', logging)
        close_i2c_bus(i2c, logging)


if __name__ == '__main__':
    main()