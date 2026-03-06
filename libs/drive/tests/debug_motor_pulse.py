import zmq
import json
import time

def test_motor_move_pulses():
    context = zmq.Context()
    socket = context.socket(zmq.PUB)
    socket.connect("ipc:///tmp/doly_control.sock")
    
    # Allow time for connection
    time.sleep(0.5)
    
    # 目标: 前进 200 个脉冲
    # 为了对比，参考单元测试参数: --pulses 50 --throttle 0.2
    # 我们用稍微大一点的 pulses, throttle 0.3
    cmd = {
        "action": "motor_move_pulses",
        "pulses": 200,
        "throttle": 0.3,
        "assume_rate": 100.0,
        "timeout": 3.0
    }
    
    topic = "io.pca9535.control"
    print(f"Sending command: {json.dumps(cmd)}")
    socket.send_multipart([topic.encode(), json.dumps(cmd).encode()])
    
    time.sleep(1) # Wait a bit
    print("Command sent.")

if __name__ == "__main__":
    test_motor_move_pulses()
