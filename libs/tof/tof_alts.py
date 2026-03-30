#!/usr/bin/env python3
"""Continuous ALS reader for paired VL6180X sensors."""

import argparse
import json
import logging
import os
import queue
import threading
import time

import adafruit_vl6180x
from adafruit_extended_bus import ExtendedI2C as I2C

from tof_common import (
    TOF_ADDR_LEFT as ADDR_LEFT,
    TOF_ADDR_RIGHT as ADDR_RIGHT,
    TOF_ENL_PIN,
    TOF_I2C_BUS as I2C_BUS,
    TOF_LUX_CALIBRATION_MARKER as LUX_CALIBRATION_MARKER,
    TOF_SETUP_MARKER as SETUP_MARKER,
    close_i2c_bus,
    setup_sensors,
    tof_en_ctl_path,
    tof_lux_calibration_json_path,
)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
EN_CTL = tof_en_ctl_path(SCRIPT_DIR)
LOG_DIR = os.path.join(os.path.dirname(os.path.dirname(SCRIPT_DIR)), 'logs')
LUX_CALIBRATION_JSON = tof_lux_calibration_json_path(SCRIPT_DIR)

os.makedirs(LOG_DIR, exist_ok=True)
logging.basicConfig(
    filename=os.path.join(LOG_DIR, f"tof_alts_{int(time.time())}.log"),
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
)


def load_lux_scales(calibration_json, logger=None):
    if not calibration_json.exists():
        return {'left_scale': 1.0, 'right_scale': 1.0}

    try:
        data = json.loads(calibration_json.read_text())
        left_scale = float(data.get('left_scale', 1.0))
        right_scale = float(data.get('right_scale', 1.0))
        if left_scale <= 0 or right_scale <= 0:
            raise ValueError('invalid non-positive scale value')
        return {'left_scale': left_scale, 'right_scale': right_scale}
    except Exception as exc:
        if logger is not None:
            logger.warning('failed to load lux calibration: %s', exc)
        return {'left_scale': 1.0, 'right_scale': 1.0}


def save_lux_scales(calibration_json, left_scale, right_scale, left_raw, right_raw, logger=None):
    payload = {
        'left_scale': left_scale,
        'right_scale': right_scale,
        'left_raw': left_raw,
        'right_raw': right_raw,
        'updated_at': int(time.time()),
    }
    calibration_json.write_text(json.dumps(payload))
    if logger is not None:
        logger.info('saved lux calibration: %s', payload)


def collect_lux_average(sensor, gain, samples, interval):
    values = []
    timeout = max(0.2, interval * 0.8)
    for _ in range(samples):
        value = safe_read_lux(sensor, gain, timeout=timeout)
        if isinstance(value, (int, float)):
            values.append(float(value))
        time.sleep(interval)

    if not values:
        return None
    return sum(values) / len(values)


def ensure_lux_calibration(args, left, right):
    
    scales = load_lux_scales(LUX_CALIBRATION_JSON, logging)
    should_calibrate = args.force_cali_lux or not LUX_CALIBRATION_MARKER.exists()
    if not should_calibrate:
        print('lux calibration marker exists; using saved scales')
        logging.info('lux calibration skipped due to marker')
        return scales

    print('请将左右 TOF 置于同一光照环境，按回车开始光照校准...')
    input()
    
    if args.force_cali_lux and LUX_CALIBRATION_MARKER.exists():
        LUX_CALIBRATION_MARKER.unlink()
        logging.info('force_cali_lux: removed marker %s', LUX_CALIBRATION_MARKER)

    left_raw = collect_lux_average(left, args.gain_left, args.cali_lux_samples, args.cali_lux_interval)
    right_raw = collect_lux_average(right, args.gain_right, args.cali_lux_samples, args.cali_lux_interval)

    if left_raw is None or right_raw is None or left_raw <= 0 or right_raw <= 0:
        print('lux calibration failed, fallback to scale=1.0')
        logging.warning('lux calibration failed: left=%s right=%s', left_raw, right_raw)
        return {'left_scale': 1.0, 'right_scale': 1.0}

    target = (left_raw + right_raw) / 2.0
    left_scale = target / left_raw
    right_scale = target / right_raw
    save_lux_scales(LUX_CALIBRATION_JSON, left_scale, right_scale, left_raw, right_raw, logging)
    LUX_CALIBRATION_MARKER.write_text(f'done {int(time.time())}\n')
    logging.info('lux calibration marker written: %s', LUX_CALIBRATION_MARKER)

    print(
        f'lux calibration done: left_raw={left_raw:.2f}, right_raw={right_raw:.2f}, '
        f'left_scale={left_scale:.4f}, right_scale={right_scale:.4f}'
    )
    return {'left_scale': left_scale, 'right_scale': right_scale}


def format_lux(value):
    if isinstance(value, (int, float)):
        return f'{float(value):.2f}'
    return str(value)


def safe_read_lux(sensor, gain, timeout=0.5):
    q = queue.Queue()

    def worker():
        try:
            q.put(('OK', sensor.read_lux(gain)))
        except Exception as exc:
            q.put(('ERR', str(exc)))

    threading.Thread(target=worker, daemon=True).start()
    try:
        status, value = q.get(timeout=timeout)
        return value if status == 'OK' else f'ERR:{value}'
    except queue.Empty:
        return 'TIMEOUT'


def main() -> None:
    parser = argparse.ArgumentParser(description='Continuous ALS reader for VL6180X')
    parser.add_argument('--interval', type=float, default=1.0, help='seconds between readings')
    parser.add_argument('--gain-left', type=int, default=adafruit_vl6180x.ALS_GAIN_40, help='ALS gain for left sensor')
    parser.add_argument('--gain-right', type=int, default=adafruit_vl6180x.ALS_GAIN_40, help='ALS gain for right sensor')
    parser.add_argument('--force-cali-lux', action='store_true', help='force rerun lux calibration and overwrite marker')
    parser.add_argument('--cali-lux-samples', type=int, default=32, help='number of lux samples used for calibration')
    parser.add_argument('--cali-lux-interval', type=float, default=0.05, help='seconds between calibration samples')
    parser.add_argument('--skip-setup', action='store_true', help='skip setup_sensors if marker present')
    parser.add_argument('--force-setup', action='store_true', help='force running setup_sensors (remove marker)')
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

    i2c = None
    left = None
    right = None

    try:
        i2c = I2C(I2C_BUS)
        left = adafruit_vl6180x.VL6180X(i2c, address=ADDR_LEFT)
        right = adafruit_vl6180x.VL6180X(i2c, address=ADDR_RIGHT)
        scales = ensure_lux_calibration(args, left, right)

        print(f'Starting continuous ALS read (interval={args.interval}s). Ctrl-C to stop.')
        print(f'Left addr=0x{ADDR_LEFT:02x} gain={args.gain_left}, Right addr=0x{ADDR_RIGHT:02x} gain={args.gain_right}')
        print(
            f"Lux scales: left={scales['left_scale']:.4f}, right={scales['right_scale']:.4f}"
        )
        while True:
            ts = time.strftime('%Y-%m-%d %H:%M:%S')
            lux_l = safe_read_lux(left, args.gain_left, timeout=args.interval * 0.8)
            lux_r = safe_read_lux(right, args.gain_right, timeout=args.interval * 0.8)

            if isinstance(lux_l, (int, float)):
                lux_l = float(lux_l) * scales['left_scale']
            if isinstance(lux_r, (int, float)):
                lux_r = float(lux_r) * scales['right_scale']

            print(f"{ts} | Left: {format_lux(lux_l)} lux | Right: {format_lux(lux_r)} lux")
            time.sleep(args.interval)
    except KeyboardInterrupt:
        print('\nStopped by user')
    finally:
        close_i2c_bus(i2c, logging)


if __name__ == '__main__':
    main()