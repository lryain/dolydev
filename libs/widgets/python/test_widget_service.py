#!/usr/bin/env python3
"""
Widget Service 测试脚本

测试 widget_service 的 ZMQ 命令接口和事件输出

用法:
    python3 test_widget_service.py [test_name]
    
测试列表:
    clock_show    - 显示时钟
    clock_hide    - 隐藏时钟
    timer_start   - 启动倒计时
    timer_pause   - 暂停定时器
    timer_resume  - 恢复定时器
    timer_stop    - 停止定时器
    subscribe     - 订阅事件
    all           - 运行所有测试
"""

import zmq
import json
import time
import sys
import threading

# Widget Service ZMQ 端点
CMD_ENDPOINT = "ipc:///tmp/doly_bus.sock"
EVENT_ENDPOINT = "ipc:///tmp/doly_widget_pub.sock"


def send_command(topic: str, payload: dict, wait_sec: float = 0.5):
    """发送 ZMQ 命令到 widget service"""
    ctx = zmq.Context()
    sock = ctx.socket(zmq.PUB)
    try:
        sock.bind(CMD_ENDPOINT)
    except zmq.ZMQError:
        sock.connect(CMD_ENDPOINT)
    time.sleep(0.5)  # slow joiner

    msg = json.dumps(payload)
    print(f"[CMD] topic={topic}")
    print(f"      payload={msg}")
    
    sock.send_multipart([
        topic.encode('utf-8'),
        msg.encode('utf-8')
    ])
    print(f"[CMD] 已发送")
    
    time.sleep(wait_sec)
    sock.close()
    ctx.term()


def subscribe_events(duration_sec: float = 10.0):
    """订阅并打印 widget 事件"""
    ctx = zmq.Context()
    sock = ctx.socket(zmq.SUB)
    sock.connect(EVENT_ENDPOINT)
    sock.subscribe(b"")  # 订阅所有
    sock.setsockopt(zmq.RCVTIMEO, 1000)  # 1秒超时

    print(f"[SUB] 订阅事件 ({duration_sec}秒)...")
    print(f"[SUB] endpoint: {EVENT_ENDPOINT}")
    
    start = time.time()
    count = 0
    try:
        while time.time() - start < duration_sec:
            try:
                topic, payload = sock.recv_multipart()
                topic_str = topic.decode('utf-8')
                try:
                    data = json.loads(payload)
                    print(f"  [EVENT] {topic_str}: {json.dumps(data, ensure_ascii=False)}")
                except json.JSONDecodeError:
                    print(f"  [EVENT] {topic_str}: {payload[:100]}")
                count += 1
            except zmq.error.Again:
                continue
    except KeyboardInterrupt:
        pass
    
    print(f"[SUB] 共收到 {count} 个事件")
    sock.close()
    ctx.term()


def test_clock_show():
    """测试显示时钟"""
    print("\n=== 测试: 显示时钟 ===")
    send_command("cmd.widget.clock.show", {
        "action": "show",
        "widget_id": "clock",
        "slot": "both",
        "timeout_ms": 10000,  # 10秒后自动隐藏
        "config": {
            "layout": "split",
            "hour_format": "24h",
            "digit_color": {"r": 0, "g": 255, "b": 0},
            "colon_blink": True
        }
    })


def test_clock_hide():
    """测试隐藏时钟"""
    print("\n=== 测试: 隐藏时钟 ===")
    send_command("cmd.widget.clock.hide", {
        "action": "hide",
        "widget_id": "clock"
    })

def test_timer_hide():
    """测试隐藏时钟"""
    print("\n=== 测试: 隐藏时钟 ===")
    send_command("cmd.widget.timer.hide", {
        "action": "hide",
        "widget_id": "timer"
    })


def test_timer_show(time: int = 5000):
    """测试显示定时器"""
    print("\n=== 测试: 显示定时器 ===")
    send_command("cmd.widget.timer.show", {
        "action": "show",
        "widget_id": "timer",
        "slot": "both",
        "timeout_ms": time,  # 3秒后自动隐藏
        "config": {
            "layout": "split",
            "hour_format": "24h",
            "digit_color": {"r": 0, "g": 255, "b": 0},
            "colon_blink": True
        }
    })

def test_timer_start(duration_sec: int = 10):
    """测试启动定时器"""
    print(f"\n=== 测试: 启动倒计时 {duration_sec}秒 ===")
    send_command("cmd.widget.timer.start", {
        "action": "start",
        "widget_id": "timer",
        "mode": "countdown",
        "duration_sec": duration_sec,
        "timeout_ms": 3000,  # 1秒后自动隐藏
        "slot": "both",
        "style": {
            "digit_color": [255, 180, 0],
            "colon_blink": True
        }
    })


def test_timer_pause():
    """测试暂停定时器"""
    print("\n=== 测试: 暂停定时器 ===")
    send_command("cmd.widget.timer.pause", {
        "action": "pause",
        "widget_id": "timer"
    })


def test_timer_resume():
    """测试恢复定时器"""
    print("\n=== 测试: 恢复定时器 ===")
    send_command("cmd.widget.timer.resume", {
        "action": "resume",
        "widget_id": "timer"
    })


def test_timer_stop():
    """测试停止定时器"""
    print("\n=== 测试: 停止定时器 ===")
    send_command("cmd.widget.timer.stop", {
        "action": "stop",
        "widget_id": "timer"
    })


def test_all():
    """运行所有测试"""
    print("=" * 60)
    print("Widget Service 完整测试")
    print("=" * 60)
    
    # 启动事件订阅（后台线程）
    sub_thread = threading.Thread(target=subscribe_events, args=(30,), daemon=True)
    sub_thread.start()
    time.sleep(1)
    
    # test_clock_show()
    # time.sleep(1)
    # test_clock_hide()
    # time.sleep(1)
    # test_clock_chime()
    # time.sleep(5)
    # test_clock_hide()
    # time.sleep(2)
    
    # test_timer_start(5)
    # test_timer_hide()
    # test_timer_show()

    # time.sleep(6)
    
    # test_timer_pause()
    # time.sleep(2)
    
    # test_timer_resume()
    # time.sleep(3)
    
    # test_timer_stop()
    # time.sleep(2)
    
    print("\n=== 所有测试完成 ===")


TESTS = {
    "clock_show": test_clock_show,
    "clock_hide": test_clock_hide,
    "timer_start": test_timer_start,
    "timer_pause": test_timer_pause,
    "timer_resume": test_timer_resume,
    "timer_stop": test_timer_stop,
    "subscribe": lambda: subscribe_events(30),
    "all": test_all,
}


if __name__ == "__main__":
    args = sys.argv[1:]
    test_name = args[0] if len(args) >= 1 else "all"

    if test_name not in TESTS:
        print(f"未知测试: {test_name}")
        print(f"可用测试: {', '.join(TESTS.keys())}")
        sys.exit(1)

    func = TESTS[test_name]
    extra_args = args[1:]
    # 尝试把额外参数转换为 int（用于秒数等）
    conv_args = []
    for a in extra_args:
        try:
            conv_args.append(int(a))
        except ValueError:
            conv_args.append(a)

    func(*conv_args)
