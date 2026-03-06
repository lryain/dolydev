#!/usr/bin/env python3
import zmq
import json
import time

def main():
    # 配置
    SUB_ENDPOINT = "ipc:///tmp/doly_zmq.sock"  # drive_service 发布事件的端点
    PUSH_ENDPOINT = "ipc:///tmp/doly_control.sock" # drive_service 接收控制的端点
    
    context = zmq.Context()
    
    # 1. 创建订阅者获取状态变化
    subscriber = context.socket(zmq.SUB)
    subscriber.connect(SUB_ENDPOINT)
    subscriber.setsockopt_string(zmq.SUBSCRIBE, "io.pca9535.")
    
    # 2. 创建推送者发送控制命令
    pusher = context.socket(zmq.PUSH)
    pusher.connect(PUSH_ENDPOINT)
    
    print(f"[*] 已连接到 DriveService")
    print(f"[*] 监听主题: io.pca9535.*")
    print(f"[*] 准备进行同步验证测试...")
    print("-" * 50)

    def send_cmd(action, params=None):
        cmd = {"action": action}
        if params:
            cmd.update(params)
        payload = json.dumps(cmd)
        print(f"[CMD] 发送控制命令: {action} | {params if params else ''}")
        # DriveService 支持 "topic content" 格式或多帧，这里使用简单的单帧补齐
        pusher.send_string(f"io.pca9535.control {payload}")

    def wait_for_event(timeout_ms=1000):
        start_time = time.time()
        while (time.time() - start_time) * 1000 < timeout_ms:
            try:
                # 尝试接收
                topic = subscriber.recv_string(flags=zmq.NOBLOCK)
                msg = subscriber.recv_string()
                data = json.loads(msg)
                print(f"[EVT] 收到事件: Topic={topic}")
                print(f"      Data: {json.dumps(data, indent=2)}")
                return data
            except zmq.Again:
                time.sleep(0.01)
        print("[!] 超时未收到事件")
        return None

    try:
        # 测试 1: 单个 IO 使能同步
        print("\n[测试 1] 验证 enable_servo 使能后的 RawState 反馈")
        send_cmd("enable_servo", {"channel": "left", "value": True})
        wait_for_event()
        
        time.sleep(5)
        
        # 测试 2: 批量 IO 设置同步
        print("\n[测试 2] 验证 set_outputs_bulk 批量修改 (使能左右舵机 bit 6 & 7)")
        # 0xC0 = 192 (binary 11000000)
        send_cmd("set_outputs_bulk", {"state": 192, "mask": 192})
        wait_for_event()
        
        time.sleep(5)

        # 测试 3: 恢复状态
        print("\n[测试 3] 恢复所有输出为关闭状态")
        send_cmd("set_outputs_bulk", {"state": 0, "mask": 192})
        wait_for_event()

    except KeyboardInterrupt:
        print("\n[*] 测试用户终止")
    finally:
        subscriber.close()
        pusher.close()
        context.term()

if __name__ == "__main__":
    main()
