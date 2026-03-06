#!/usr/bin/env python3
"""订阅并打印 status.fan.state（调试用）"""
import zmq
import time
import json
import sys

# 注意：命令端点和状态端点现在是不同的
COMMAND_ENDPOINT = "ipc:///tmp/doly_fan_zmq.sock"
STATUS_ENDPOINT = "ipc:///tmp/doly_zmq_status.sock"

# 可以通过命令行参数指定端点
if len(sys.argv) > 1:
    STATUS_ENDPOINT = sys.argv[1]

CTX = zmq.Context()
SUB = CTX.socket(zmq.SUB)
SUB.setsockopt(zmq.LINGER, 0)
SUB.connect(STATUS_ENDPOINT)
# 在 connect 之后设置 SUBSCRIBE 选项
SUB.setsockopt_string(zmq.SUBSCRIBE, "status.fan.state")
SUB.setsockopt(zmq.RCVTIMEO, 5000)

print(f"Subscribed to {STATUS_ENDPOINT} for status.fan.state")
try:
    while True:
        try:
            topic = SUB.recv_string()
            payload = SUB.recv_string()
            try:
                j = json.loads(payload)
            except Exception:
                j = payload
            print(f"[{time.time()}] {topic} -> {json.dumps(j, ensure_ascii=False)}")
        except zmq.Again:
            # timeout
            continue
except KeyboardInterrupt:
    print('Exiting')
