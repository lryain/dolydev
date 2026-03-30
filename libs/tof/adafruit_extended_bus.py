#!/usr/bin/env python3
from __future__ import annotations

import importlib.machinery
import importlib.util
import sys
from pathlib import Path


def _load_real_module():
    current_dir = Path(__file__).resolve().parent
    search_paths: list[str] = []

    for entry in sys.path:
        try:
            resolved = Path(entry or ".").resolve()
        except Exception:
            continue
        if resolved == current_dir:
            continue
        search_paths.append(entry)

    spec = importlib.machinery.PathFinder.find_spec("adafruit_extended_bus", search_paths)
    if spec is None or spec.loader is None:
        return None

    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


_REAL_MODULE = _load_real_module()

if _REAL_MODULE is not None:
    ExtendedI2C = _REAL_MODULE.ExtendedI2C
else:
    class ExtendedI2C:
        def __init__(self, bus_id: int, *_args, **_kwargs) -> None:
            self.bus_id = int(bus_id)
            self.channel = self.bus_id

        def deinit(self) -> None:
            return None

        def close(self) -> None:
            return None

        def __repr__(self) -> str:
            return f"ExtendedI2C(bus_id={self.bus_id})"


__all__ = ["ExtendedI2C"]