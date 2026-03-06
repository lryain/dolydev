#!/usr/bin/env python3
"""
测试 serial_service 的 ZMQ 消息发布
"""

import zmq
import json
import time

def test_serial_zmq():
    # 连接到 serial_service 的发布端点
    context = zmq.Context()
    subscriber = context.socket(zmq.SUB)
    subscriber.connect("ipc:///tmp/doly_serial_pub.sock")
    subscriber.setsockopt_string(zmq.SUBSCRIBE, "")  # 订阅所有主题

    print("订阅者已连接到 ipc:///tmp/doly_serial_pub.sock")

    # 等待消息
    print("等待消息...")
    try:
        while True:
            # 接收主题
            topic_msg = subscriber.recv()
            topic = topic_msg.decode('utf-8')

            # 接收负载
            payload_msg = subscriber.recv()
            payload = json.loads(payload_msg.decode('utf-8'))

            print(f"收到消息 - 主题: {topic}")
            print(f"负载: {json.dumps(payload, indent=2)}")
            print("-" * 50)

    except KeyboardInterrupt:
        print("测试结束")
    finally:
        subscriber.close()
        context.term()

if __name__ == "__main__":
    test_serial_zmq()