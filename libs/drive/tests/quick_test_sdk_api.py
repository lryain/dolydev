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
        # ("电机前进 1s", "io.pca9535.control", {"action": "motor_forward", "speed": 0.30, "duration": 1.0}),
        # ("电机后退 1s", "io.pca9535.control", {"action": "motor_backward", "speed": 0.30, "duration": 1.0}),
        # ("电机左转 0.8s", "io.pca9535.control", {"action": "motor_turn_left", "speed": 0.25, "duration": 0.8}),
        # ("电机右转 0.8s", "io.pca9535.control", {"action": "motor_turn_right", "speed": 0.25, "duration": 0.8}),
        # ("厘米级前进", "io.pca9535.control", {"action": "move_distance_cm", "distance_cm": 5.0, "throttle": 0.30, "timeout_ms": 10000}),
        # ("角度转向", "io.pca9535.control", {"action": "turn_deg", "angle_deg": 45.0, "throttle": 0.35, "timeout_ms": 3000}),
        # ("电机停止", "io.pca9535.control", {"action": "motor_stop"}),
        
        ("LED 关闭", "io.pca9535.control", {"action": "led_off"}),

        ("go_xy 固定回归", "io.pca9535.control", {
            "action": "go_xy",
            "x": 100,
            "y": 50,
            "speed": 30,
            "toForward": True,
            "with_brake": True,
            "acceleration_interval": 3,
            "control_speed": True,
            "control_force": True,
            "timeout_ms": 12000,
        }),
        ("drive_rotate 完整参数回归", "io.pca9535.control", {
            "action": "drive_rotate",
            "angle_deg": 45.0,
            "from_center": True,
            "speed": 30,
            "toForward": True,
            "with_brake": True,
            "acceleration_interval": 2,
            "control_speed": False,
            "control_force": True,
            "timeout_ms": 5000,
        }),
        ("turn_deg 完整参数回归", "io.pca9535.control", {
            "action": "turn_deg",
            "angle_deg": -30.0,
            "from_center": True,
            "speed": 28,
            "toForward": False,
            "with_brake": True,
            "acceleration_interval": 1,
            "control_speed": False,
            "control_force": True,
            "timeout_ms": 5000,
        }),
        ("drive_distance 完整参数回归", "io.pca9535.control", {
            "action": "drive_distance",
            "distance_mm": 100,
            "speed": 30,
            "toForward": True,
            "with_brake": True,
            "acceleration_interval": 2,
            "control_speed": True,
            "control_force": True,
            "timeout_ms": 7000,
        }),

        ("SDK 执行旋转45度", "io.pca9535.control", {"action": "sdk_go_rotate", "rotateAngle": 45.0, "from_center": True, "speed": 30, "toForward": True}),
        ("SDK 执行旋转-45度", "io.pca9535.control", {"action": "sdk_go_rotate", "rotateAngle": -45.0, "from_center": True, "speed": 30, "toForward": True}),
        ("SDK 执行前进50mm", "io.pca9535.control", {"action": "sdk_go_distance", "mm": 50, "speed": 35, "toForward": False}),
        ("SDK 执行XY移动", "io.pca9535.control", {"action": "sdk_go_xy", "x": 100, "y": 50, "speed": 30}),

        # # 新增：测试带时长的特效（持续 2000ms）
        # ("LED 呼吸 2s", "io.pca9535.control", {"action": "set_led_effect", "effect": "breath", "r": 0, "g": 255, "b": 255, "duration_ms": 2000}),
        # ("等待 3s", None, 3),
        # ("检查恢复 (设置默认)", "io.pca9535.control", {"action": "set_led_color", "r": 0, "g": 50, "b": 0}),
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
