#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Simple obstacle-avoidance cruise control using paired VL6180X sensors."""

import argparse
import configparser
import logging
import os
import sys
import time

from adafruit_extended_bus import ExtendedI2C as I2C
from adafruit_motor import motor
from adafruit_pca9685 import PCA9685
import adafruit_vl6180x

from tof_common import (
    TOF_ADDR_LEFT as ADDR_LEFT,
    TOF_ADDR_RIGHT as ADDR_RIGHT,
    TOF_ENL_PIN,
    TOF_I2C_BUS as I2C_BUS,
    close_i2c_bus,
    tof_en_ctl_path,
    setup_sensors,
)

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, '..', '..'))
CONFIG_PATH = os.path.join(ROOT, 'config', 'tof_cruise.ini')
LOG_DIR = os.path.join(ROOT, 'logs')
EN_CTL = tof_en_ctl_path(SCRIPT_DIR)

os.makedirs(LOG_DIR, exist_ok=True)
logging.basicConfig(
    filename=os.path.join(LOG_DIR, f"tof_cruise_{int(time.time())}.log"),
    level=logging.INFO,
    format='%(asctime)s [%(levelname)s] %(message)s',
)


def load_cfg():
    cfg = configparser.ConfigParser()
    cfg['cruise'] = {
        'obstacle_mm': '60',
        'forward_speed': '0.4',
        'turn_speed': '0.4',
        'turn_time_s': '0.35',
        'loop_delay_s': '0.05',
    }
    if os.path.exists(CONFIG_PATH):
        cfg.read(CONFIG_PATH)
    cruise = cfg['cruise']
    return {
        'obs': cruise.getint('obstacle_mm', fallback=60),
        'fwd': cruise.getfloat('forward_speed', fallback=0.6),
        'turn': cruise.getfloat('turn_speed', fallback=0.6),
        'turn_t': cruise.getfloat('turn_time_s', fallback=0.35),
        'loop': cruise.getfloat('loop_delay_s', fallback=0.05),
    }


def motors_init():
    i2c = I2C(3)
    pca = PCA9685(i2c, address=0x40)
    pca.frequency = 100
    left_motor = motor.DCMotor(pca.channels[12], pca.channels[13])
    right_motor = motor.DCMotor(pca.channels[15], pca.channels[14])
    return pca, left_motor, right_motor


def drive(left_motor, right_motor, left_throttle, right_throttle, direction_scale):
    left_motor.throttle = max(-1.0, min(1.0, left_throttle * direction_scale))
    right_motor.throttle = max(-1.0, min(1.0, right_throttle * direction_scale))


def stop(left_motor, right_motor):
    left_motor.throttle = 0
    right_motor.throttle = 0


def main() -> None:
    parser = argparse.ArgumentParser(description='TOF cruise with obstacle avoidance')
    parser.add_argument(
        '--motor-direction',
        choices=('normal', 'reverse'),
        default='normal',
        help='set motor forward direction mapping',
    )
    args = parser.parse_args()

    direction_scale = -1.0 if args.motor_direction == 'reverse' else 1.0

    # if not os.path.exists(EN_CTL):
    #     print('[ERR] build/tof_en_ctl missing. Run build.sh first.')
    #     sys.exit(2)

    setup_sensors(
        i2c_bus=I2C_BUS,
        addr_left=ADDR_LEFT,
        addr_right=ADDR_RIGHT,
        en_ctl=EN_CTL,
        tof_en_pin=TOF_ENL_PIN,
        setup_marker=None,
        logger=logging,
    )

    i2c6 = None
    pca = None
    left_motor = None
    right_motor = None
    left = None
    right = None
    cfg = load_cfg()

    print('Cruising... Ctrl-C to stop')
    try:
        i2c6 = I2C(I2C_BUS)
        left = adafruit_vl6180x.VL6180X(i2c6, address=ADDR_LEFT)
        right = adafruit_vl6180x.VL6180X(i2c6, address=ADDR_RIGHT)
        pca, left_motor, right_motor = motors_init()

        while True:
            try:
                dl = left.range
            except Exception as exc:
                logging.error('read left range failed: %s', exc)
                dl = 9999
            try:
                dr = right.range
            except Exception as exc:
                logging.error('read right range failed: %s', exc)
                dr = 9999

            logging.info('L=%d R=%d', dl, dr)
            if dl <= cfg['obs'] or dr <= cfg['obs']:
                if dl < dr:
                    drive(left_motor, right_motor, cfg['turn'], -cfg['turn'], direction_scale)
                else:
                    drive(left_motor, right_motor, -cfg['turn'], cfg['turn'], direction_scale)
                time.sleep(cfg['turn_t'])
            else:
                drive(left_motor, right_motor, cfg['fwd'], cfg['fwd'], direction_scale)
            time.sleep(cfg['loop'])
    except KeyboardInterrupt:
        logging.info('keyboard interrupt received, stopping cruise')
    finally:
        if left_motor is not None and right_motor is not None:
            stop(left_motor, right_motor)
        if pca is not None:
            pca.deinit()
        close_i2c_bus(i2c6, logging)


if __name__ == '__main__':
    main()