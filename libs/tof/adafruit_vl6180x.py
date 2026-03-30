#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Local VL6180X compatibility wrapper backed by 3rd/vl6180_pi."""

from __future__ import annotations

import ctypes
import re
import subprocess
import threading
from pathlib import Path
from typing import Any


__version__ = "0.0.0+native.1"
__repo__ = "https://github.com/adafruit/Adafruit_CircuitPython_VL6180X.git"

_VL6180X_REG_IDENTIFICATION_MODEL_ID = 0x000
_VL6180X_REG_SYSTEM_HISTORY_CTRL = 0x012
_VL6180X_REG_SYSTEM_INTERRUPT_CONFIG = 0x014
_VL6180X_REG_SYSTEM_INTERRUPT_CLEAR = 0x015
_VL6180X_REG_SYSTEM_FRESH_OUT_OF_RESET = 0x016
_VL6180X_REG_SYSRANGE_START = 0x018
_VL6180X_REG_SYSRANGE_INTERMEASUREMENT_PERIOD = 0x01B
_VL6180X_REG_SYSRANGE_PART_TO_PART_RANGE_OFFSET = 0x024
_VL6180X_REG_SYSALS_START = 0x038
_VL6180X_REG_SYSALS_ANALOGUE_GAIN = 0x03F
_VL6180X_REG_SYSALS_INTEGRATION_PERIOD_HI = 0x040
_VL6180X_REG_SYSALS_INTEGRATION_PERIOD_LO = 0x041
_VL6180X_REG_RESULT_RANGE_STATUS = 0x04D
_VL6180X_REG_RESULT_INTERRUPT_STATUS_GPIO = 0x04F
_VL6180X_REG_RESULT_ALS_VAL = 0x050
_VL6180X_REG_RESULT_HISTORY_BUFFER_0 = 0x052
_VL6180X_REG_RESULT_RANGE_VAL = 0x062
_VL6180X_REG_I2C_SLAVE_DEVICE_ADDRESS = 0x212

_VL6180X_DEFAULT_I2C_ADDR = 0x29
_HISTORY_LENGTH = 16
_DEFAULT_TIMEOUT_MS = 500

ALS_GAIN_1 = 0x06
ALS_GAIN_1_25 = 0x05
ALS_GAIN_1_67 = 0x04
ALS_GAIN_2_5 = 0x03
ALS_GAIN_5 = 0x02
ALS_GAIN_10 = 0x01
ALS_GAIN_20 = 0x00
ALS_GAIN_40 = 0x07

ERROR_NONE = 0
ERROR_SYSERR_1 = 1
ERROR_SYSERR_5 = 5
ERROR_ECEFAIL = 6
ERROR_NOCONVERGE = 7
ERROR_RANGEIGNORE = 8
ERROR_SNR = 11
ERROR_RAWUFLOW = 12
ERROR_RAWOFLOW = 13
ERROR_RANGEUFLOW = 14
ERROR_RANGEOFLOW = 15

_BUILD_LOCK = threading.Lock()
_LIB_LOCK = threading.Lock()
_LIB_HANDLE = None


def _native_dir() -> Path:
    return Path(__file__).resolve().parents[2] / "3rd" / "vl6180_pi"


def _native_lib_path() -> Path:
    return _native_dir() / "libvl6180_pi.so"


def _ensure_native_library() -> Path:
    lib_path = _native_lib_path()
    if lib_path.exists():
        return lib_path

    with _BUILD_LOCK:
        if lib_path.exists():
            return lib_path
        subprocess.check_call(["make"], cwd=str(_native_dir()))

    if not lib_path.exists():
        raise RuntimeError(f"native VL6180 library not built: {lib_path}")
    return lib_path


def _load_library() -> ctypes.CDLL:
    global _LIB_HANDLE

    with _LIB_LOCK:
        if _LIB_HANDLE is not None:
            return _LIB_HANDLE

        lib_path = _ensure_native_library()
        library = ctypes.CDLL(str(lib_path))

        library.vl6180_initialise_address.argtypes = [ctypes.c_int, ctypes.c_int]
        library.vl6180_initialise_address.restype = ctypes.c_int
        library.vl6180_close.argtypes = [ctypes.c_int]
        library.vl6180_close.restype = None
        library.vl6180_change_addr.argtypes = [ctypes.c_int, ctypes.c_int]
        library.vl6180_change_addr.restype = ctypes.c_int
        library.vl6180_read_register_8.argtypes = [ctypes.c_int, ctypes.c_uint16]
        library.vl6180_read_register_8.restype = ctypes.c_int
        library.vl6180_read_register_16.argtypes = [ctypes.c_int, ctypes.c_uint16]
        library.vl6180_read_register_16.restype = ctypes.c_int
        library.vl6180_write_register_8.argtypes = [ctypes.c_int, ctypes.c_uint16, ctypes.c_uint8]
        library.vl6180_write_register_8.restype = ctypes.c_int
        library.vl6180_write_register_16.argtypes = [ctypes.c_int, ctypes.c_uint16, ctypes.c_uint16]
        library.vl6180_write_register_16.restype = ctypes.c_int
        library.vl6180_start_range_continuous.argtypes = [ctypes.c_int, ctypes.c_int]
        library.vl6180_start_range_continuous.restype = ctypes.c_int
        library.vl6180_stop_range_continuous.argtypes = [ctypes.c_int]
        library.vl6180_stop_range_continuous.restype = ctypes.c_int
        library.vl6180_continuous_mode_enabled.argtypes = [ctypes.c_int]
        library.vl6180_continuous_mode_enabled.restype = ctypes.c_int
        library.vl6180_get_distance_continuous.argtypes = [ctypes.c_int, ctypes.c_int]
        library.vl6180_get_distance_continuous.restype = ctypes.c_int
        library.vl6180_enable_range_history.argtypes = [ctypes.c_int]
        library.vl6180_enable_range_history.restype = ctypes.c_int
        library.vl6180_range_history_enabled.argtypes = [ctypes.c_int]
        library.vl6180_range_history_enabled.restype = ctypes.c_int
        library.vl6180_get_range_from_history.argtypes = [ctypes.c_int]
        library.vl6180_get_range_from_history.restype = ctypes.c_int
        library.vl6180_get_ranges_from_history.argtypes = [ctypes.c_int, ctypes.POINTER(ctypes.c_uint8), ctypes.c_int]
        library.vl6180_get_ranges_from_history.restype = ctypes.c_int
        library.vl6180_set_offset.argtypes = [ctypes.c_int, ctypes.c_int]
        library.vl6180_set_offset.restype = ctypes.c_int
        library.vl6180_get_offset.argtypes = [ctypes.c_int]
        library.vl6180_get_offset.restype = ctypes.c_int
        library.vl6180_range_status.argtypes = [ctypes.c_int]
        library.vl6180_range_status.restype = ctypes.c_int
        library.get_distance.argtypes = [ctypes.c_int]
        library.get_distance.restype = ctypes.c_int
        library.get_ambient_light.argtypes = [ctypes.c_int, ctypes.c_int]
        library.get_ambient_light.restype = ctypes.c_float

        _LIB_HANDLE = library
        return _LIB_HANDLE


def _parse_bus_number_from_text(text: str) -> int | None:
    match = re.search(r"/dev/i2c-(\d+)", text)
    if match:
        return int(match.group(1))
    return None


def _coerce_bus_number(value: Any) -> int | None:
    if isinstance(value, bool):
        return None
    if isinstance(value, int):
        return int(value)
    if isinstance(value, str):
        return _parse_bus_number_from_text(value)
    if value is None:
        return None
    name = getattr(value, "name", None)
    if isinstance(name, str):
        return _parse_bus_number_from_text(name)
    return None


def _extract_bus_number(i2c: Any) -> int:
    direct_names = (
        "bus_id",
        "bus",
        "bus_num",
        "bus_number",
        "i2c_bus",
        "channel",
    )
    nested_chains = (
        ("_i2c", "_i2c_bus", "_device", "name"),
        ("_i2c_bus", "_device", "name"),
        ("_device", "name"),
    )

    bus_number = _coerce_bus_number(i2c)
    if bus_number is not None:
        return bus_number

    for name in direct_names:
        try:
            bus_number = _coerce_bus_number(getattr(i2c, name))
        except Exception:
            continue
        if bus_number is not None:
            return bus_number

    for chain in nested_chains:
        value = i2c
        try:
            for name in chain:
                value = getattr(value, name)
        except Exception:
            continue
        bus_number = _coerce_bus_number(value)
        if bus_number is not None:
            return bus_number

    for text in (repr(i2c), str(i2c)):
        bus_number = _parse_bus_number_from_text(text)
        if bus_number is not None:
            return bus_number

    raise TypeError(f"unable to determine I2C bus number from {type(i2c)!r}")


class VL6180X:
    def __init__(self, i2c: Any, address: int = _VL6180X_DEFAULT_I2C_ADDR, offset: int = 0) -> None:
        self._lib = _load_library()
        self._bus_number = _extract_bus_number(i2c)
        self._address = int(address)
        self._handle = self._lib.vl6180_initialise_address(self._bus_number, self._address)
        self._closed = False

        if self._handle < 0:
            raise RuntimeError(
                f"Could not find VL6180X on /dev/i2c-{self._bus_number} at 0x{self._address:02x}"
            )

        self._check_rc(self._lib.vl6180_enable_range_history(self._handle), "enable history")
        self.offset = offset

    def __del__(self) -> None:
        try:
            self.close()
        except Exception:
            pass

    def close(self) -> None:
        if getattr(self, "_closed", True):
            return
        self._lib.vl6180_close(self._handle)
        self._closed = True

    def deinit(self) -> None:
        self.close()

    def _ensure_open(self) -> None:
        if self._closed:
            raise RuntimeError("VL6180X handle is closed")

    def _check_rc(self, rc: int, operation: str) -> None:
        if rc < 0:
            raise RuntimeError(f"{operation} failed with code {rc}")

    def _read_int(self, value: int, operation: str) -> int:
        if value < 0:
            raise RuntimeError(f"{operation} failed with code {value}")
        return int(value)

    @property
    def range(self) -> int:
        if self.continuous_mode_enabled:
            return self._read_range_continuous()
        return self._read_range_single()

    @property
    def range_from_history(self) -> int | None:
        if not self.range_history_enabled:
            return None
        self._ensure_open()
        value = self._lib.vl6180_get_range_from_history(self._handle)
        return self._read_int(value, "read range history")

    @property
    def ranges_from_history(self) -> list[int] | None:
        if not self.range_history_enabled:
            return None

        self._ensure_open()
        buffer = (ctypes.c_uint8 * _HISTORY_LENGTH)()
        count = self._lib.vl6180_get_ranges_from_history(self._handle, buffer, _HISTORY_LENGTH)
        self._check_rc(count, "read range history buffer")
        return [int(buffer[index]) for index in range(count)]

    @property
    def range_history_enabled(self) -> bool:
        self._ensure_open()
        return bool(self._lib.vl6180_range_history_enabled(self._handle))

    def start_range_continuous(self, period: int = 100) -> None:
        if not 20 <= period <= 2550:
            raise ValueError(
                "Delay must be in 10 millisecond increments between 20 and 2550 milliseconds"
            )
        self._ensure_open()
        self._check_rc(self._lib.vl6180_start_range_continuous(self._handle, int(period)), "start continuous range")

    def stop_range_continuous(self) -> None:
        self._ensure_open()
        self._check_rc(self._lib.vl6180_stop_range_continuous(self._handle), "stop continuous range")

    @property
    def continuous_mode_enabled(self) -> bool:
        self._ensure_open()
        return bool(self._lib.vl6180_continuous_mode_enabled(self._handle))

    @property
    def offset(self) -> int:
        self._ensure_open()
        value = self._lib.vl6180_get_offset(self._handle)
        return self._read_int(value, "read offset")

    @offset.setter
    def offset(self, offset: int) -> None:
        if not -128 <= int(offset) <= 127:
            raise ValueError("offset must be between -128 and 127 mm")
        self._ensure_open()
        self._check_rc(self._lib.vl6180_set_offset(self._handle, int(offset)), "set offset")

    def _read_range_single(self) -> int:
        self._ensure_open()
        value = self._lib.get_distance(self._handle)
        return self._read_int(value, "read single-shot range")

    def _read_range_continuous(self) -> int:
        self._ensure_open()
        value = self._lib.vl6180_get_distance_continuous(self._handle, _DEFAULT_TIMEOUT_MS)
        return self._read_int(value, "read continuous range")

    def read_lux(self, gain: int) -> float:
        self._ensure_open()
        lux = float(self._lib.get_ambient_light(self._handle, min(int(gain), ALS_GAIN_40)))
        if lux < 0.0:
            raise RuntimeError("read lux failed")
        return lux

    @property
    def range_status(self) -> int:
        self._ensure_open()
        value = self._lib.vl6180_range_status(self._handle)
        return self._read_int(value, "read range status")

    def _write_8(self, address: int, data: int) -> None:
        self._ensure_open()
        if int(address) == _VL6180X_REG_I2C_SLAVE_DEVICE_ADDRESS:
            self._check_rc(self._lib.vl6180_change_addr(self._handle, int(data)), "change device address")
            self._address = int(data)
            return
        self._check_rc(
            self._lib.vl6180_write_register_8(self._handle, int(address), int(data) & 0xFF),
            f"write register 0x{int(address):04x}",
        )

    def _write_16(self, address: int, data: int) -> None:
        self._ensure_open()
        self._check_rc(
            self._lib.vl6180_write_register_16(self._handle, int(address), int(data) & 0xFFFF),
            f"write register 0x{int(address):04x}",
        )

    def _read_8(self, address: int) -> int:
        self._ensure_open()
        value = self._lib.vl6180_read_register_8(self._handle, int(address))
        return self._read_int(value, f"read register 0x{int(address):04x}")

    def _read_16(self, address: int) -> int:
        self._ensure_open()
        value = self._lib.vl6180_read_register_16(self._handle, int(address))
        return self._read_int(value, f"read register 0x{int(address):04x}")


__all__ = [
    "ALS_GAIN_1",
    "ALS_GAIN_1_25",
    "ALS_GAIN_1_67",
    "ALS_GAIN_2_5",
    "ALS_GAIN_5",
    "ALS_GAIN_10",
    "ALS_GAIN_20",
    "ALS_GAIN_40",
    "ERROR_NONE",
    "ERROR_SYSERR_1",
    "ERROR_SYSERR_5",
    "ERROR_ECEFAIL",
    "ERROR_NOCONVERGE",
    "ERROR_RANGEIGNORE",
    "ERROR_SNR",
    "ERROR_RAWUFLOW",
    "ERROR_RAWOFLOW",
    "ERROR_RANGEUFLOW",
    "ERROR_RANGEOFLOW",
    "VL6180X",
]