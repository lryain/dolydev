#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
左右两颗 VL6180X 实时测距打印：
- 左传感器带使能（TOF_ENL 在 PCA9535 上），通过 Doly GPIO 驱动的 C++ 小工具控制
- 右传感器无使能线，视为常开
- 使用 I2C6: /dev/i2c-6，基地址 0x29，给右传感器改地址为 0x2A
- 如果安装的 vl6180x-multi 不支持 None 占位，这里在逻辑层做兼容
"""
import os
import sys
import time
import logging
import argparse

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

log_dir = os.path.join(os.path.dirname(os.path.dirname(SCRIPT_DIR)), 'logs')
os.makedirs(log_dir, exist_ok=True)
logging.basicConfig(
    filename=os.path.join(log_dir, f"tof_demo_{int(time.time())}.log"),
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s'
)

OFFSET_JSON = tof_offsets_json_path(SCRIPT_DIR)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--skip-setup', action='store_true', help='skip setup_sensors if marker present')
    parser.add_argument('--force-setup', action='store_true', help='force running setup_sensors (remove marker)')
    parser.add_argument('--side', choices=('left', 'right', 'both'), default='both',
                        help='select which TOF sensor(s) to test')
    args = parser.parse_args()

    if args.force_setup:
        try:
            if SETUP_MARKER.exists():
                SETUP_MARKER.unlink()
                logging.info("force_setup: removed marker %s", SETUP_MARKER)
        except Exception as e:
            logging.warning("force_setup: failed to remove marker: %s", e)

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
    i2c = I2C(I2C_BUS)

    # retry initialization after setup
    try:
        left = None
        right = None
        if args.side in ('left', 'both'):
            left = adafruit_vl6180x.VL6180X(i2c, address=ADDR_LEFT, offset=offsets['left'])
        if args.side in ('right', 'both'):
            right = adafruit_vl6180x.VL6180X(i2c, address=ADDR_RIGHT, offset=offsets['right'])
    except Exception as e2:
        logging.error("Sensor init failed: %s", e2)
        print("Sensor initialization failed:", e2)
        sys.exit(5)

    logging.info("Applied offsets left=%d right=%d", offsets['left'], offsets['right'])

    if left is not None and right is not None:
        print(f"当前左侧偏移量: {left.offset} mm, 右侧偏移量: {right.offset} mm")
    elif left is not None:
        print(f"当前左侧偏移量: {left.offset} mm")
    elif right is not None:
        print(f"当前右侧偏移量: {right.offset} mm")

    print("TOF demo running. Ctrl-C to exit.")
    try:
        while True:
            if left is not None and right is not None:
                dl = left.range
                dr = right.range
                print(f"L={dl:4d} mm | R={dr:4d} mm")
                logging.info("L=%d R=%d", dl, dr)
            elif left is not None:
                dl = left.range
                print(f"L={dl:4d} mm")
                logging.info("L=%d", dl)
            elif right is not None:
                dr = right.range
                print(f"R={dr:4d} mm")
                logging.info("R=%d", dr)
            time.sleep(0.05)
    except KeyboardInterrupt:
        pass
    finally:
        # Adafruit driver has no explicit close; rely on GC
        pass


if __name__ == '__main__':
    main()
