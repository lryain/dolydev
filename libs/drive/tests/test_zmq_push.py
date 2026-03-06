#!/usr/bin/env python3
"""测试 ZeroMQ PUSH/PULL 通信"""

import zmq
import json
import time

def test_push():
    context = zmq.Context()
    socket = context.socket(zmq.PUSH)
    
    # 连接到 drive_service 的 PULL socket
    endpoint = "ipc:///tmp/doly_control.sock"
    socket.connect(endpoint)
    
    print(f"Connected to {endpoint}")
    time.sleep(2)  # 等待连接建立（增加到2秒）
    
    # 发送测试命令
    topic = "io.pca9535.control.enable_servo_left"
    command = {
        "action": "enable_servo_left",
        "value": True,
        "timestamp": int(time.time() * 1000),
        "source": "test_script"
    }
    
    # 发送 multipart message
    socket.send_string(topic, zmq.SNDMORE)
    socket.send_string(json.dumps(command))
    
    print(f"✓ Sent: {topic}")
    print(f"  Payload: {json.dumps(command)}")
    
    time.sleep(3)  # 等待消息被处理（增加到3秒）
    
    socket.close()
    print("✓ Socket closed")
    # context.term()  # 不调用 term()，避免卡住
    print("✓ Test completed")

if __name__ == "__main__":
    test_push()
