#!/usr/bin/env python3
"""简单的 Fan ZMQ 控制发布脚本
用法示例：
  ./send_fan_cmd.py set_pwm --pwm 1500 --duration_ms 5000
  ./send_fan_cmd.py inhibit --id audio --duration_ms 3000
  ./send_fan_cmd.py uninhibit --id audio
  ./send_fan_cmd.py enable --enabled 0
  ./send_fan_cmd.py mode --mode quiet
  ./send_fan_cmd.py persistent-mode --mode quiet
  ./send_fan_cmd.py clear-mode

"""
import argparse
import json
import time
import zmq

DEFAULT_ENDPOINT = "ipc:///tmp/doly_fan_zmq.sock"

parser = argparse.ArgumentParser()
subparsers = parser.add_subparsers(dest="cmd")

p = subparsers.add_parser("set_pwm")
p.add_argument("--pwm", type=int, required=False)
p.add_argument("--pct", type=float, required=False)
p.add_argument("--duration_ms", type=int, default=0)

p = subparsers.add_parser("inhibit")
p.add_argument("--id", type=str, default="audio")
p.add_argument("--duration_ms", type=int, default=0)

p = subparsers.add_parser("uninhibit")
p.add_argument("--id", type=str, required=True)

p = subparsers.add_parser("enable")
p.add_argument("--enabled", type=int, choices=[0,1], default=1)

p = subparsers.add_parser("mode")
p.add_argument("--mode", type=str, choices=["quiet","normal","performance"], default="normal")
p.add_argument("--duration_ms", type=int, default=0, help="如果指定，该时间后回到自动控制；否则持久保持此模式")

p = subparsers.add_parser("persistent-mode")
p.add_argument("--mode", type=str, choices=["quiet","normal","performance"], default="normal")

p = subparsers.add_parser("clear-mode")

p = subparsers.add_parser("send_raw")
p.add_argument("topic")
p.add_argument("payload")

parser.add_argument("--endpoint", type=str, default=DEFAULT_ENDPOINT)

args = parser.parse_args()

ctx = zmq.Context.instance()
pub = ctx.socket(zmq.PUB)
pub.setsockopt(zmq.LINGER, 0)
# 客户端连接到服务提供者的端点
pub.connect(args.endpoint)
# 给连接一点时间来建立
time.sleep(0.05)

if args.cmd == "set_pwm":
    payload = {}
    if args.pwm is not None:
        payload["pwm"] = args.pwm
    if args.pct is not None:
        payload["pct"] = args.pct
    payload["duration_ms"] = args.duration_ms
    topic = "cmd.fan.set"
elif args.cmd == "inhibit":
    payload = {"id": args.id, "duration_ms": args.duration_ms}
    topic = "cmd.fan.inhibit"
elif args.cmd == "uninhibit":
    payload = {"id": args.id}
    topic = "cmd.fan.uninhibit"
elif args.cmd == "enable":
    payload = {"enabled": bool(args.enabled)}
    topic = "cmd.fan.enable"
elif args.cmd == "mode":
    payload = {"mode": args.mode}
    if args.duration_ms > 0:
        payload["duration_ms"] = args.duration_ms
    topic = "cmd.fan.mode"
elif args.cmd == "persistent-mode":
    # 持久模式：一直保持此模式，直到收到新命令
    payload = {"mode": args.mode}
    topic = "cmd.fan.persistent-mode"
elif args.cmd == "clear-mode":
    # 清除模式：回到自动控制
    payload = {}
    topic = "cmd.fan.clear-mode"
elif args.cmd == "send_raw":
    topic = args.topic
    try:
        payload = json.loads(args.payload)
    except Exception:
        payload = args.payload
else:
    parser.print_help()
    raise SystemExit(1)

msg = json.dumps(payload)
print(f"Publishing {topic} -> {msg}")
pub.send_string(topic, zmq.SNDMORE)
pub.send_string(msg)

