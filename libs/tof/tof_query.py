#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Simple REQ client for tof_service.py."""

from __future__ import annotations

import argparse
import json
import sys

import zmq


def main() -> int:
    parser = argparse.ArgumentParser(description="Query passive TOF service")
    parser.add_argument("--endpoint", default="tcp://127.0.0.1:5568", help="service endpoint")
    parser.add_argument("--cmd", default="get_tof", choices=("ping", "get_tof", "get_imu", "get_snapshot", "health"))
    parser.add_argument("--include-lux", action="store_true", help="include lux fields for TOF queries")
    args = parser.parse_args()

    context = zmq.Context()
    socket = context.socket(zmq.REQ)
    socket.setsockopt(zmq.RCVTIMEO, 1000)
    socket.setsockopt(zmq.SNDTIMEO, 1000)
    socket.setsockopt(zmq.LINGER, 0)
    socket.connect(args.endpoint)
    try:
        socket.send_string(json.dumps({"cmd": args.cmd, "include_lux": args.include_lux}, ensure_ascii=True))
        response = json.loads(socket.recv_string())
    finally:
        socket.close(0)
        context.term()

    print(json.dumps(response, indent=2, ensure_ascii=False, sort_keys=True))
    return 0 if response.get("ok") else 1


if __name__ == "__main__":
    sys.exit(main())