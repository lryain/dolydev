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
        # 编码器脉冲移动测试（需要先启用编码器）
        ("电机前进100脉冲", "io.pca9535.control", {"action": "motor_move_pulses", "pulses": 100, "throttle": 0.2}),
        ("电机前进100脉冲", "io.pca9535.control", {"action": "motor_move_pulses", "pulses": -100, "throttle": 0.2}),
        
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
