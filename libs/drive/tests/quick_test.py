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
        # ("LED 绿色", "io.pca9535.control", {"action": "set_led_color", "r": 0, "g": 255, "b": 0}),
        # ("LED 蓝色", "io.pca9535.control", {"action": "set_led_color", "r": 0, "g": 0, "b": 255}),
        # ("LED 黄色", "io.pca9535.control", {"action": "set_led_color", "r": 255, "g": 255, "b": 0}),
        # ("LED 橙色", "io.pca9535.control", {"action": "set_led_color", "r": 255, "g": 165, "b": 0}),
        # ("LED 紫色", "io.pca9535.control", {"action": "set_led_color", "r": 255, "g": 0, "b": 255}),
        # ("LED 青色呼吸", "io.pca9535.control", {"action": "set_led_effect", "effect": "blink", "r": 255, "g": 0, "b": 0}),
        # ("LED 绿色", "io.pca9535.control", {"action": "set_led_color", "r": 255, "g": 255, "b": 255}),
        # ("LED 青色呼吸", "io.pca9535.control", {"action": "set_led_effect", "effect": "breath", "r": 255, "g": 255, "b": 0}),
        ("LED 青色呼吸", "io.pca9535.control", {"action": "set_led_effect", "effect": "breath", "r": 255, "g": 255, "b": 255}),
        
        ("电机左转1s", "io.pca9535.control", {"action": "motor_turn_left", "speed": 0.3, "duration": 1.0}),
        ("电机右转1s", "io.pca9535.control", {"action": "motor_turn_right", "speed": 0.3, "duration": 1.0}),
        # ("电机左转1s", "io.pca9535.control", {"action": "motor_turn_left", "speed": 0.3, "duration": 1.0}),
        # ("电机右转1s", "io.pca9535.control", {"action": "motor_turn_right", "speed": 0.3, "duration": 1.0}),
        # ("电机停止", "io.pca9535.control", {"action": "motor_stop"}),

        # 编码器脉冲移动测试（需要先启用编码器）
        # ("电机前进100脉冲", "io.pca9535.control", {"action": "motor_move_pulses", "pulses": 100, "throttle": 0.4}),
        # ("电机前进100脉冲", "io.pca9535.control", {"action": "motor_move_pulses", "pulses": -100, "throttle": 0.4}),
        
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
