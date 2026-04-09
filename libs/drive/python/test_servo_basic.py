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
        ("LED 红色", "io.pca9535.control", {"action": "set_led_color", "r": 255, "g": 0, "b": 0}),
        ("设置舵机角度", "io.pca9535.control", {
            "action": "set_servo_multi",
            "targets": {"left": 10, "right": 10},
            "speed": 15
        }),
        ("设置舵机角度", "io.pca9535.control", {
            "action": "set_servo_multi",
            "targets": {"left": 90, "right": 90},
            "speed": 15
        }),
        ("设置舵机角度", "io.pca9535.control", {
            "action": "set_servo_multi",
            "targets": {"left": 180, "right": 180},
            "speed": 15
        }),
        ("设置舵机角度", "io.pca9535.control", {
            "action": "set_servo_multi",
            "targets": {"left": 5, "right": 5},
            "speed": 15
        }),
        # ("禁用舵机", "io.pca9535.control", {"action": "enable_servo", "channel": "both", "value": False}),
        ("检查恢复 (设置默认)", "io.pca9535.control", {"action": "set_led_color", "r": 0, "g": 50, "b": 0}),
        
        ("LED 关闭", "io.pca9535.control", {"action": "led_off"}),

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
