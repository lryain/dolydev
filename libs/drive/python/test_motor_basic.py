#!/usr/bin/env python3 
"""快速测试 Drive 命令（使用 IPC socket）"""
import zmq
import json
import time
import sys

def test_drive_commands():
    ctx = zmq.Context()
    push = ctx.socket(zmq.PUSH)
    push.connect('ipc:///tmp/doly_control.sock')
    
    print("✅ 连接到 ipc:///tmp/doly_control.sock")
    time.sleep(0.5)  # 等待连接建立

    tests = [
        # ("LED 红色", "io.pca9535.control", {"action": "set_led_color", "r": 255, "g": 0, "b": 0}),
        ("LED 青色呼吸", "io.pca9535.control", {"action": "set_led_effect", "effect": "breath", "r": 255, "g": 255, "b": 255}),
        # ("电机左转1s", "io.pca9535.control", {"action": "motor_turn_left", "speed": 0.25, "duration": 1.0}),
        # ("电机停止", "io.pca9535.control", {"action": "motor_stop"}),
        # ("电机左转1s", "io.pca9535.control", {"action": "motor_turn_left", "speed": 0.25, "duration": 1.0}),
        # ("电机停止", "io.pca9535.control", {"action": "motor_stop"}),
        
        # ("电机左转1s", "io.pca9535.control", {"action": "motor_turn_left", "speed": 0.5, "duration": 1.0}),
        # ("电机停止", "io.pca9535.control", {"action": "motor_stop"}),
        # ("电机右转1s", "io.pca9535.control", {"action": "motor_turn_right", "speed": 0.5, "duration": 1.0}),
        # ("电机停止", "io.pca9535.control", {"action": "motor_stop"}),
        # ("电机左转1s", "io.pca9535.control", {"action": "motor_turn_left", "speed": 0.3, "duration": 1.0}),
        # ("电机右转1s", "io.pca9535.control", {"action": "motor_turn_right", "speed": 0.3, "duration": 1.0}),
        # ("电机停止", "io.pca9535.control", {"action": "motor_stop"}),

        # ("厘米级前进", "io.pca9535.control", {"action": "move_distance_cm", "distance_cm": 5.0, "throttle": 0.35, "timeout_ms": 10000}),
        # ("厘米级前进", "io.pca9535.control", {"action": "move_distance_cm", "distance_cm": -5.0, "throttle": 0.35, "timeout_ms": 10000}),

        ("SDK 执行旋转45度", "io.pca9535.control", {"action": "drive_rotate", "angle_deg": 35.0, "is_center": False, "speed": 30, "toForward": True}),
        ("SDK 执行旋转45度", "io.pca9535.control", {"action": "drive_rotate", "angle_deg": -35.0, "is_center": False, "speed": 30, "toForward": True}),
        # ("SDK 执行旋转45度", "io.pca9535.control", {"action": "sdk_go_rotate", "rotateAngle": 45.0, "from_center": True, "speed": 60, "toForward": True}),
        # ("等待 3s", None, 3),
        # ("SDK 执行旋转-45度", "io.pca9535.control", {"action": "sdk_go_rotate", "rotateAngle": -45.0, "from_center": True, "speed": 60, "toForward": True}),
        # ("等待 3s", None, 3),
        
        ("LED 关闭", "io.pca9535.control", {"action": "led_off"}),
    ]
    
    for name, topic, cmd in tests:
        if topic is None:
            # 特殊步骤：等待一段时间（cmd 为秒数）
            print(f"⏳ {name} for {cmd}s")
            time.sleep(float(cmd))
            continue

        push.send_string(topic, zmq.SNDMORE)
        push.send_string(json.dumps(cmd))
        print(f"✅ 发送: {name}")
        # 普通步骤之间保持 1 秒间隔
        time.sleep(1)
    
    print("\n✅ 所有命令已发送")
    push.close()
    ctx.term()

if __name__ == "__main__":
    try:
        test_drive_commands()
    except KeyboardInterrupt:
        print("\n❌ 测试中断")
    except Exception as e:
        print(f"❌ 错误: {e}")
        sys.exit(1)
