#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
时钟小组件 ZMQ 测试脚本
- 查询当前时间(get_time)
- 触发报时(announce_time)

用法示例：
    python widget_clock_api_test.py get_time
    python widget_clock_api_test.py get_time --count 5 --interval 1.0
    python widget_clock_api_test.py announce_time --lang en

注意：
- 请先确保 EyeEngine 已启动且 clock widget 的 api.enabled=true。
- 默认端点与代码一致：ipc:///tmp/doly_clock_api.sock。
- 需要依赖 pyzmq：pip install pyzmq
"""

import argparse
import json
import sys
import time
from typing import Any, Dict, Optional

import zmq


def send_request(endpoint: str, payload: Dict[str, Any], timeout_ms: int = 1000) -> Dict[str, Any]:
    """发送单次 REQ/REP 请求并返回 JSON 响应。"""
    ctx = zmq.Context.instance()
    sock = ctx.socket(zmq.REQ)
    sock.setsockopt(zmq.RCVTIMEO, timeout_ms)
    sock.setsockopt(zmq.SNDTIMEO, timeout_ms)
    sock.setsockopt(zmq.LINGER, 0)
    sock.connect(endpoint)
    try:
        msg = json.dumps(payload)
        sock.send_string(msg)
        resp = sock.recv_string()
        return json.loads(resp)
    except zmq.error.Again:
        return {"error": "timeout"}
    except Exception as e:  # noqa: BLE001
        return {"error": str(e)}
    finally:
        sock.close(0)


def do_get_time(args: argparse.Namespace) -> None:
    payload = build_payload("get_time", args.lang)
    for i in range(args.count):
        resp = send_request(args.endpoint, payload, args.timeout)
        print(f"[{i+1}/{args.count}] get_time -> {resp}")
        if i + 1 < args.count:
            time.sleep(args.interval)


def do_chime_now(args: argparse.Namespace) -> None:
    payload = build_payload("chime_now", args.lang)
    resp = send_request(args.endpoint, payload, args.timeout)
    print(f"chime_now -> {resp}")


def do_announce_time(args: argparse.Namespace) -> None:
    payload = build_payload("announce_time", args.lang)
    resp = send_request(args.endpoint, payload, args.timeout)
    print(f"announce_time -> {resp}")


def build_payload(action: str, lang: Optional[str]) -> Dict[str, Any]:
    payload = {"action": action}
    if lang:
        payload["language"] = "zh" if lang == "cn" else lang
    return payload


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="ClockWidget ZMQ 测试")
    parser.add_argument("action", choices=["get_time", "chime_now", "announce_time"], help="测试动作")
    parser.add_argument("--lang", choices=["en", "cn"], help="指定语言")
    parser.add_argument("--endpoint", "-e", default="ipc:///tmp/doly_clock_api.sock",
                        help="ZMQ REP 端点，默认 ipc:///tmp/doly_clock_api.sock")
    parser.add_argument("--timeout", type=int, default=1000, help="请求超时 ms，默认 1000")
    parser.add_argument("--count", type=int, default=1, help="重复次数（仅 get_time 有效）")
    parser.add_argument("--interval", type=float, default=1.0, help="重复间隔秒（仅 get_time 有效）")
    return parser


def main() -> None:
    parser = build_parser()
    args = parser.parse_args()

    # 将 endpoint/action 解包后路由到具体函数
    class CallArgs:
        pass

    call_args = CallArgs()
    call_args.endpoint = args.endpoint
    call_args.timeout = args.timeout
    call_args.lang = args.lang

    if args.action == "get_time":
        call_args.count = max(1, args.count)
        call_args.interval = max(0.0, args.interval)
        do_get_time(call_args)
    elif args.action == "chime_now":
        do_chime_now(call_args)
    elif args.action == "announce_time":
        do_announce_time(call_args)
    else:  # 理论不会进入
        sys.exit(f"未知 action: {args.action}")


if __name__ == "__main__":
    main()
