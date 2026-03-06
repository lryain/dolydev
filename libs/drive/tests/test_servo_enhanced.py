#!/usr/bin/env python3
"""增强版舵机指令冒烟测试
在 drive_service 运行且连接 ipc:///tmp/doly_control.sock 时执行。
"""
import json
import time
import zmq


def main():
    ctx = zmq.Context.instance()
    sock = ctx.socket(zmq.PUSH)
    sock.connect("ipc:///tmp/doly_control.sock")
    time.sleep(0.2)

    def send(topic, payload, desc=""):
        sock.send_string(topic, zmq.SNDMORE)
        sock.send_string(json.dumps(payload))
        print(f"✅ 发送 {desc or payload}")
        time.sleep(0.6)

    # ("同时开启左右舵机", "io.pca9535.control", {"action": "enable_servo", "channel": "both", "value": True}),
    # send("io.pca9535.control", {
    #     "action": "enable_servo",
    #     "channel": "both",
    #     "value": True
    # }, "同时开启左右舵机")

    # 同步多路
    send("io.pca9535.control", {
        "action": "set_servo_multi",
        "angles": {"left": 60, "right": 120},
        "speed": 60,
    }, "同步多路 60/120")

    # 预设 center
    send("io.pca9535.control", {
        "action": "set_servo_preset",
        "name": "center",
        "speed": 70,
    }, "预设 center")

    # 快速异步
    send("io.pca9535.control", {
        "action": "set_servo_multi",
        "angles": {"left": 0, "right": 180},
        "speed": 90,
    }, "异步快切 0/180")

    # 状态查询
    send("io.pca9535.control", {
        "action": "get_servo_status"
    }, "查询状态")

    sock.close()
    ctx.term()


if __name__ == "__main__":
    main()
