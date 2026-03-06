#!/usr/bin/env python3
"""监听小智状态/情绪更新的测试脚本"""

import json
import zmq


def main():
    context = zmq.Context()
    socket = context.socket(zmq.SUB)
    socket.connect("ipc:///tmp/doly_xiaozhi_state.sock")
    socket.subscribe(b"status.xiaozhi.state")
    socket.subscribe(b"status.xiaozhi.emotion")

    print("[monitor_emotion] 已连接，等待状态或情绪更新... Ctrl+C 退出")
    try:
        while True:
            topic, payload = socket.recv_multipart()
            topic_str = topic.decode("utf-8", errors="replace")
            try:
                payload_json = json.loads(payload)
            except json.JSONDecodeError:
                payload_json = {"raw": payload.decode("utf-8", errors="replace")}
            print(f"[monitor_emotion] {topic_str} -> {json.dumps(payload_json, ensure_ascii=False)}")
    except KeyboardInterrupt:
        print("[monitor_emotion] 停止监听")
    finally:
        socket.close()
        context.term()


if __name__ == "__main__":
    main()
