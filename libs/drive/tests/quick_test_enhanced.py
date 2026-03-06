#!/usr/bin/env python3
"""增强舵机控制测试脚本"""
import json
import time
import zmq


def test_enhanced_servo():
    ctx = zmq.Context()
    push = ctx.socket(zmq.PUSH)
    push.connect('ipc:///tmp/doly_control.sock')
    print("✅ 连接到 ipc:///tmp/doly_control.sock")
    time.sleep(0.5)

    topic = "io.pca9535.control"

    def send(cmd):
        push.send_string(topic, zmq.SNDMORE)
        push.send_string(json.dumps(cmd))
        print(f"👉 发送: {cmd['action']}")
        time.sleep(0.1)

    send({
        "action": "enable_servo",
        "channel": "both",
        "value": True
    })
    # 1. 多舵机移动 (速度控制)
    print("\n--- 测试 1: 多舵机移动 (Speed=40, 先慢后快) ---")
    send({
        "action": "set_servo_multi",
        "targets": {"left": 0, "right": 0},
        "speed": 80
    })
    time.sleep(1)
    send({
        "action": "set_servo_multi",
        "targets": {"left": 90, "right": 90},
        "speed": 30
    })
    time.sleep(1)

    # 2. 指定时间移动
    print("\n--- 测试 2: 指定时间移动 (Duration=500ms) ---")
    send({
        "action": "set_servo_multi",
        "targets": {"left": 180, "right": 180},
        "duration": 500
    })
    time.sleep(1)

    # 3. 摆动模式 (Swing)
    print("\n--- 测试 3: 摆动模式 (Swing) ---")
    send({
        "action": "servo_swing",
        "channel": "left",
        "min": 45,
        "max": 90,
        "duration": 400,
        "count": 5
    })
    time.sleep(0.2)
    send({
        "action": "servo_swing",
        "channel": "right",
        "min": 135,
        "max": 45,
        "duration": 400,
        "count": 5
    })
    time.sleep(4)

    # 4. servo_swing_of 测试：先定位再以原角度为中心摆动
    print("\n--- 测试 4: servo_swing_of (先定位再固定幅度摆动) ---")
    send({
        "action": "servo_swing_of",
        "channel": "left",
        "target": 30,
        "approach_speed": 40,
        "amplitude": 60,
        "swing_speed": 30,
        "count": 8
    })
    time.sleep(5)

    # 5. 急停
    print("\n--- 测试 5: 启动后立即急停 ---")
    send({
        "action": "servo_swing",
        "channel": "left",
        "min": 0,
        "max": 180,
        "duration": 1000,
        "count": -1
    })
    time.sleep(2)
    print("🛑 发送停止命令")
    send({"action": "servo_stop", "channel": "all"})
    send({
        "action": "enable_servo",
        "channel": "both",
        "value": False
    })
    print("\n✅ 测试完成")
    push.close()
    ctx.term()


if __name__ == "__main__":
    test_enhanced_servo()#!/usr/bin/env python3 
"""增强舵机控制测试脚本"""
import zmq
import json
import time

def test_enhanced_servo():
    ctx = zmq.Context()
    push = ctx.socket(zmq.PUSH)
    push.connect('ipc:///tmp/doly_control.sock')
    print("✅ 连接到 ipc:///tmp/doly_control.sock")
    time.sleep(0.5)

    topic = "io.pca9535.control"

    # 5. 急停
    print("--- 测试 5: 启动并急停 ---")
    def send(cmd):
        push.send_string(topic, zmq.SNDMORE)
        push.send_string(json.dumps(cmd))
        print(f"👉 发送: {cmd['action']}")
        time.sleep(0.1)
    
    # send({
    #     "action": "enable_servo",
    #     "channel": "both",
    #     "value": True
    # })

    # 1. 普通多舵机移动 (速度控制)
    print("\n--- 测试 1: 多舵机移动 (Speed=30, 较慢) ---")
    send({
        "action": "set_servo_multi", 
        "targets": {"left": 0, "right": 0}, 
        "speed": 80
    })
    time.sleep(0.1)
    
    # 4. servo_swing_of 测试
    print("--- 测试 4: servo_swing_of (先定位再固定幅度摆动) ---")
    send({
        "action": "servo_swing_of",
        "channel": "left",
        "target": 30,
        "approach_speed": 400,
        "amplitude": 60,
        "swing_speed": 30,
        "count": 6
    })
    time.sleep(5)

    # send({
    #     "action": "set_servo_multi", 
    #     "targets": {"left": 90, "right": 90}, 
    #     "speed": 30
    # })
    # time.sleep(1)

    # 2. 指定时间移动 (Duration)
    # print("\n--- 测试 2: 指定时间移动 (Duration=2000ms) ---")
    # send({
    #     "action": "set_servo_multi", 
    #     "targets": {"left": 0, "right": 180},
    #     "duration": 2000
    # })
    # time.sleep(2.5)
    # send({
    #     "action": "set_servo_multi", 
    #     "targets": {"left": 180, "right": 180}, 
    #     "speed": 30
    # })
    # time.sleep(1)

    # send({
    #     "action": "set_servo_multi", 
    #     "targets": {"left": 90, "right": 90},
    #     "duration": 500
    # })
    # time.sleep(1)

    # # 3. 摆动模式 (Swing)
    # print("\n--- 测试 3: 摆动模式 (Swing) ---")
    # # 左手摆动
    # send({
    #     "action": "servo_swing",
    #     "channel": "left",
    #     "min": 45,
    #     "max": 90,
    #     "duration": 400,
    #     "count": 5
    # })
    # # 右手同时摆动 (反相)
    # time.sleep(0.2)
    # send({
    #     "action": "servo_swing",
    #     "channel": "right",
    #     "min": 90,
    #     "max": 45,
    #     "duration": 400,
    #     "count": 5
    # })
    
    # time.sleep(1)

    # send({
    #     "action": "set_servo_multi", 
    #     "targets": {"left": 170, "right": 170},
    #     "duration": 500
    # })
    # time.sleep(1)

    # # 3. 摆动模式 (Swing)
    # print("\n--- 测试 3: 摆动模式 (Swing) ---")
    # # 左手摆动
    # send({
    #     "action": "servo_swing",
    #     "channel": "left",
    #     "min": 145,
    #     "max": 180,
    #     "duration": 200,
    #     "count": 10
    # })
    # # 右手同时摆动 (反相)
    # time.sleep(0.0)
    # send({
    #     "action": "servo_swing",
    #     "channel": "right",
    #     "min": 180,
    #     "max": 145,
    #     "duration": 200,
    #     "count": 10
    # })
    
    # time.sleep(5.0)

    # # 4. 急停
    # print("\n--- 测试 4: 启动并急停 ---")
    # send({
    #     "action": "servo_swing",
    #     "channel": "left",
    #     "min": 0,
    #     "max": 180,
    #     "duration": 1000,
    #     "count": 10
    # })
    # time.sleep(2)
    # 复位
    send({
        "action": "set_servo_multi", 
        "targets": {"left": 0, "right": 0}, 
        "speed": 80
    })
    time.sleep(1)
    print("🛑 发送停止命令")
    send({"action": "servo_stop", "channel": "all"})

    print("\n✅ 测试完成")
    push.close()
    ctx.term()

if __name__ == "__main__":
    test_enhanced_servo()
