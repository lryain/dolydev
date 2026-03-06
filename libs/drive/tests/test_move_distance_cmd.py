#!/usr/bin/env python3
"""
测试 move_distance_cm 命令
验证编码器自动初始化修复
"""

import zmq
import json
import time
import sys

def test_move_distance_cm():
    context = zmq.Context()
    socket = context.socket(zmq.PUSH)
    socket.connect("tcp://localhost:5555")
    
    print("=" * 60)
    print("Testing move_distance_cm command")
    print("=" * 60)
    
    # 测试1: 正向移动 10cm
    print("\n[TEST 1] Moving forward 10cm with throttle=0.1")
    cmd1 = {
        "action": "move_distance_cm",
        "distance_cm": 10,
        "throttle": 0.1,
        "timeout_ms": 10000
    }
    socket.send_json({
        "topic": "io.pca9535.control",
        "cmd": cmd1
    })
    print(f"Sent: {json.dumps(cmd1, indent=2)}")
    time.sleep(2)
    
    # 测试2: 反向移动 10cm
    print("\n[TEST 2] Moving backward 10cm with throttle=0.1")
    cmd2 = {
        "action": "move_distance_cm",
        "distance_cm": -10,
        "throttle": 0.1,
        "timeout_ms": 10000
    }
    socket.send_json({
        "topic": "io.pca9535.control",
        "cmd": cmd2
    })
    print(f"Sent: {json.dumps(cmd2, indent=2)}")
    time.sleep(2)
    
    # 测试3: 更大的距离
    print("\n[TEST 3] Moving forward 20cm with throttle=0.2")
    cmd3 = {
        "action": "move_distance_cm",
        "distance_cm": 20,
        "throttle": 0.2,
        "timeout_ms": 15000
    }
    socket.send_json({
        "topic": "io.pca9535.control",
        "cmd": cmd3
    })
    print(f"Sent: {json.dumps(cmd3, indent=2)}")
    time.sleep(3)
    
    print("\n" + "=" * 60)
    print("All test commands sent.")
    print("Check service logs with: journalctl -u drive-service -f")
    print("=" * 60)
    
    socket.close()
    context.term()

if __name__ == "__main__":
    try:
        test_move_distance_cm()
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)
