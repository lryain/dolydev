#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Passive ZMQ service for paired VL6180X sensors."""

from __future__ import annotations

import argparse
import json
import logging
import signal
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Any

import yaml
import zmq

from tof_common import (
    TOF_ADDR_LEFT,
    TOF_ADDR_RIGHT,
    TOF_ENL_PIN,
    TOF_I2C_BUS,
    TOF_SETUP_MARKER,
    close_i2c_bus,
    load_offsets,
    setup_sensors,
    tof_en_ctl_path,
    tof_offsets_json_path,
)

try:
    from adafruit_extended_bus import ExtendedI2C as I2C
    import adafruit_vl6180x
except Exception:  # pragma: no cover - hardware dependency
    I2C = None
    adafruit_vl6180x = None


ALS_GAIN_MAP: dict[int, int] = {}
if adafruit_vl6180x is not None:
    ALS_GAIN_MAP = {
        1: adafruit_vl6180x.ALS_GAIN_1,
        2: adafruit_vl6180x.ALS_GAIN_2_5,
        5: adafruit_vl6180x.ALS_GAIN_5,
        10: adafruit_vl6180x.ALS_GAIN_10,
        20: adafruit_vl6180x.ALS_GAIN_20,
        40: adafruit_vl6180x.ALS_GAIN_40,
    }


@dataclass
class ServiceConfig:
    bind_endpoint: str = "tcp://127.0.0.1:5568"
    poll_timeout_ms: int = 100
    idle_close_ms: int = 1500
    cache_ttl_ms: int = 60
    log_level: str = "INFO"
    mock_mode: bool = False
    auto_setup: bool = True
    tof_enabled: bool = True
    tof_i2c_bus: int = TOF_I2C_BUS
    tof_left_address: int = TOF_ADDR_LEFT
    tof_right_address: int = TOF_ADDR_RIGHT
    tof_left_enable_pin: int = TOF_ENL_PIN
    tof_setup_marker: str = str(TOF_SETUP_MARKER)
    tof_offset_file: str = str(tof_offsets_json_path(Path(__file__).resolve().parent))
    tof_en_ctl_binary: str = tof_en_ctl_path(Path(__file__).resolve().parent)
    tof_read_lux_by_default: bool = False
    tof_als_gain: int = 40
    obstacle_threshold_mm: int = 150
    imu_enabled: bool = True
    imu_timeout_ms: int = 1500
    mock_left_mm: int = 118
    mock_right_mm: int = 121
    mock_lux_left: float = 42.0
    mock_lux_right: float = 44.0
    mock_roll: float = 0.0
    mock_pitch: float = 0.0
    mock_yaw: float = 179.2


def load_config(path: str) -> ServiceConfig:
    raw = yaml.safe_load(Path(path).read_text(encoding="utf-8")) or {}
    node = raw.get("tof_service", raw)
    config = ServiceConfig()

    for key in (
        "bind_endpoint",
        "poll_timeout_ms",
        "idle_close_ms",
        "cache_ttl_ms",
        "log_level",
        "mock_mode",
        "auto_setup",
    ):
        if key in node:
            setattr(config, key, node[key])

    tof = node.get("tof", {})
    config.tof_enabled = tof.get("enabled", config.tof_enabled)
    config.tof_i2c_bus = tof.get("i2c_bus", config.tof_i2c_bus)
    config.tof_left_address = tof.get("left_address", config.tof_left_address)
    config.tof_right_address = tof.get("right_address", config.tof_right_address)
    config.tof_left_enable_pin = tof.get("left_enable_pin", config.tof_left_enable_pin)
    config.tof_setup_marker = tof.get("setup_marker", config.tof_setup_marker)
    config.tof_offset_file = tof.get("offset_file", config.tof_offset_file)
    config.tof_en_ctl_binary = tof.get("en_ctl_binary", config.tof_en_ctl_binary)
    config.tof_read_lux_by_default = tof.get("read_lux_by_default", config.tof_read_lux_by_default)
    config.tof_als_gain = tof.get("als_gain", config.tof_als_gain)
    config.obstacle_threshold_mm = tof.get("obstacle_threshold_mm", config.obstacle_threshold_mm)

    imu = node.get("imu", {})
    config.imu_enabled = imu.get("enabled", config.imu_enabled)
    config.imu_timeout_ms = imu.get("timeout_ms", config.imu_timeout_ms)

    mock = node.get("mock", {})
    mock_tof = mock.get("tof", {})
    mock_imu = mock.get("imu", {})
    config.mock_left_mm = mock_tof.get("left_mm", config.mock_left_mm)
    config.mock_right_mm = mock_tof.get("right_mm", config.mock_right_mm)
    config.mock_lux_left = mock_tof.get("lux_left", config.mock_lux_left)
    config.mock_lux_right = mock_tof.get("lux_right", config.mock_lux_right)
    config.mock_roll = mock_imu.get("roll", config.mock_roll)
    config.mock_pitch = mock_imu.get("pitch", config.mock_pitch)
    config.mock_yaw = mock_imu.get("yaw", config.mock_yaw)
    return config


class TofHardware:
    def __init__(self, config: ServiceConfig, logger: logging.Logger) -> None:
        self._config = config
        self._logger = logger
        self._i2c: Any = None
        self._left: Any = None
        self._right: Any = None
        self._last_access_ms = 0
        self._cached_at_ms = 0
        self._cached_snapshot: dict[str, Any] | None = None
        self._open_count = 0

    @property
    def open_count(self) -> int:
        return self._open_count

    def _ensure_ready(self) -> None:
        if self._config.mock_mode:
            return
        if self._left is not None and self._right is not None:
            return
        if I2C is None or adafruit_vl6180x is None:
            raise RuntimeError("adafruit_vl6180x dependencies are unavailable")

        if self._config.auto_setup:
            setup_sensors(
                i2c_bus=self._config.tof_i2c_bus,
                addr_left=self._config.tof_left_address,
                addr_right=self._config.tof_right_address,
                en_ctl=self._config.tof_en_ctl_binary,
                tof_en_pin=self._config.tof_left_enable_pin,
                setup_marker=Path(self._config.tof_setup_marker),
                auto_confirm=True,
                logger=self._logger,
            )

        offsets = load_offsets(Path(self._config.tof_offset_file), self._logger)
        self._i2c = I2C(self._config.tof_i2c_bus)
        self._left = adafruit_vl6180x.VL6180X(self._i2c, address=self._config.tof_left_address, offset=offsets["left"])
        self._right = adafruit_vl6180x.VL6180X(self._i2c, address=self._config.tof_right_address, offset=offsets["right"])
        self._open_count += 1
        self._logger.info(
            "TOF hardware ready: left=0x%02x right=0x%02x",
            self._config.tof_left_address,
            self._config.tof_right_address,
        )

    def close_idle_if_needed(self, now_ms: int) -> None:
        if self._config.mock_mode:
            return
        if self._left is None and self._right is None:
            return
        if now_ms - self._last_access_ms < self._config.idle_close_ms:
            return
        self.close()

    def close(self) -> None:
        if self._left is not None:
            close = getattr(self._left, "close", None)
            if callable(close):
                close()
        if self._right is not None:
            close = getattr(self._right, "close", None)
            if callable(close):
                close()
        self._left = None
        self._right = None
        if self._i2c is not None:
            close_i2c_bus(self._i2c, self._logger)
        self._i2c = None
        self._cached_snapshot = None
        self._cached_at_ms = 0

    def snapshot(self, include_lux: bool) -> dict[str, Any]:
        now_ms = int(time.monotonic() * 1000)
        self._last_access_ms = now_ms
        if self._cached_snapshot is not None and (now_ms - self._cached_at_ms) <= self._config.cache_ttl_ms:
            snapshot = dict(self._cached_snapshot)
            snapshot["cached"] = True
            return snapshot

        if self._config.mock_mode:
            snapshot = {
                "valid": True,
                "left_valid": True,
                "right_valid": True,
                "left_mm": self._config.mock_left_mm,
                "right_mm": self._config.mock_right_mm,
                "min_distance_mm": min(self._config.mock_left_mm, self._config.mock_right_mm),
                "balance_error_mm": self._config.mock_left_mm - self._config.mock_right_mm,
                "obstacle_detected": min(self._config.mock_left_mm, self._config.mock_right_mm) < self._config.obstacle_threshold_mm,
                "source": "vl6180x",
                "cached": False,
            }
            if include_lux:
                snapshot["lux_left"] = self._config.mock_lux_left
                snapshot["lux_right"] = self._config.mock_lux_right
            self._cached_snapshot = dict(snapshot)
            self._cached_at_ms = now_ms
            return snapshot

        self._ensure_ready()
        left_mm = int(self._left.range)
        right_mm = int(self._right.range)
        snapshot = {
            "valid": True,
            "left_valid": left_mm > 0,
            "right_valid": right_mm > 0,
            "left_mm": left_mm,
            "right_mm": right_mm,
            "min_distance_mm": min(left_mm, right_mm),
            "balance_error_mm": left_mm - right_mm,
            "obstacle_detected": min(left_mm, right_mm) < self._config.obstacle_threshold_mm,
            "source": "vl6180x",
            "cached": False,
        }
        if include_lux:
            gain = ALS_GAIN_MAP.get(self._config.tof_als_gain, ALS_GAIN_MAP.get(40))
            if gain is not None:
                snapshot["lux_left"] = float(self._left.read_lux(gain))
                snapshot["lux_right"] = float(self._right.read_lux(gain))
        self._cached_snapshot = dict(snapshot)
        self._cached_at_ms = now_ms
        return snapshot


class ImuProvider:
    def __init__(self, config: ServiceConfig) -> None:
        self._config = config

    def snapshot(self) -> dict[str, Any]:
        if self._config.mock_mode:
            return {
                "valid": True,
                "roll": self._config.mock_roll,
                "pitch": self._config.mock_pitch,
                "yaw": self._config.mock_yaw,
                "source": "mock",
            }
        return {
            "valid": False,
            "source": "not_implemented",
            "reason": "use drive shared_state or future adapter",
        }


class PassiveSensorService:
    def __init__(self, config: ServiceConfig) -> None:
        self._config = config
        self._logger = logging.getLogger("tof_service")
        self._tof = TofHardware(config, self._logger)
        self._imu = ImuProvider(config)
        self._request_count = 0
        self._error_count = 0
        self._running = True

    def stop(self, *_args: Any) -> None:
        self._running = False

    def _handle_request(self, request: dict[str, Any]) -> dict[str, Any]:
        cmd = request.get("cmd", "")
        include_lux = bool(request.get("include_lux", self._config.tof_read_lux_by_default))
        if cmd == "ping":
            return {
                "ok": True,
                "service": "tof_service",
                "mode": "mock" if self._config.mock_mode else "hardware",
            }
        if cmd == "get_tof":
            return {"ok": True, "timestamp_ms": int(time.time() * 1000), "tof": self._tof.snapshot(include_lux)}
        if cmd == "get_imu":
            return {"ok": True, "timestamp_ms": int(time.time() * 1000), "imu": self._imu.snapshot()}
        if cmd == "get_snapshot":
            return {
                "ok": True,
                "timestamp_ms": int(time.time() * 1000),
                "tof": self._tof.snapshot(include_lux),
                "imu": self._imu.snapshot(),
            }
        if cmd == "health":
            return {
                "ok": True,
                "stats": {
                    "request_count": self._request_count,
                    "error_count": self._error_count,
                    "tof_open_count": self._tof.open_count,
                },
                "config": {
                    "bind_endpoint": self._config.bind_endpoint,
                    "mock_mode": self._config.mock_mode,
                    "cache_ttl_ms": self._config.cache_ttl_ms,
                },
            }
        return {"ok": False, "error": f"unsupported cmd: {cmd}"}

    def serve_forever(self) -> int:
        context = zmq.Context()
        socket = context.socket(zmq.REP)
        socket.bind(self._config.bind_endpoint)
        poller = zmq.Poller()
        poller.register(socket, zmq.POLLIN)
        self._logger.info("TOF service listening on %s", self._config.bind_endpoint)
        try:
            while self._running:
                events = dict(poller.poll(self._config.poll_timeout_ms))
                now_ms = int(time.monotonic() * 1000)
                self._tof.close_idle_if_needed(now_ms)
                if socket not in events:
                    continue
                self._request_count += 1
                try:
                    request = json.loads(socket.recv_string())
                    response = self._handle_request(request)
                except Exception as exc:
                    self._error_count += 1
                    self._logger.exception("request handling failed")
                    response = {"ok": False, "error": str(exc)}
                socket.send_string(json.dumps(response, ensure_ascii=True))
        finally:
            self._tof.close()
            socket.close(0)
            context.term()
        return 0


def build_arg_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Passive TOF ZMQ service")
    parser.add_argument("--config", default="/home/pi/dolydev/config/tof_service.yaml", help="YAML config path")
    parser.add_argument("--bind", help="override bind endpoint")
    parser.add_argument("--mock", action="store_true", help="force mock mode")
    return parser


def main() -> int:
    args = build_arg_parser().parse_args()
    config = load_config(args.config)
    if args.bind:
        config.bind_endpoint = args.bind
    if args.mock:
        config.mock_mode = True

    logging.basicConfig(
        level=getattr(logging, str(config.log_level).upper(), logging.INFO),
        format="%(asctime)s [%(levelname)s] %(message)s",
    )

    service = PassiveSensorService(config)
    signal.signal(signal.SIGINT, service.stop)
    signal.signal(signal.SIGTERM, service.stop)
    return service.serve_forever()


if __name__ == "__main__":
    sys.exit(main())