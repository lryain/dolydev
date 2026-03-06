#!/usr/bin/env python3
"""动作编排测试脚本 - 举哑铃、挥旗、打鼓、划桨等动作"""
import json
import time
import zmq


def test_action_choreography():
    ctx = zmq.Context()
    push = ctx.socket(zmq.PUSH)
    push.connect('ipc:///tmp/doly_control.sock')
    print("✅ 连接到 ipc:///tmp/doly_control.sock")
    time.sleep(0.5)

    topic = "io.pca9535.control"

    def send(cmd):
        push.send_string(topic, zmq.SNDMORE)
        push.send_string(json.dumps(cmd))
        print(f"👉 发送: {cmd['action']}")
        time.sleep(0.1)

    # 启用舵机
    send({
        "action": "enable_servo",
        "channel": "both",
        "value": True
    })
    time.sleep(0.5)

    # 测试 1: 轻哑铃 - 快速，大幅度
    print("\n=== 测试 1: 轻哑铃举重 (weight=20) ===")
    send({
        "action": "lift_dumbbell",
        "channel": "left",
        "weight": 20,
        "reps": 5
    })
    time.sleep(8)

    # 测试 2: 重哑铃 - 慢速，小幅度
    print("\n=== 测试 2: 重哑铃举重 (weight=80) ===")
    send({
        "action": "lift_dumbbell",
        "channel": "right",
        "weight": 80,
        "reps": 3
    })
    time.sleep(10)

    # 测试 3: 双手交替举哑铃舞蹈 - 中等重量
    print("\n=== 测试 3: 双手交替举哑铃舞蹈 (weight=40, 8秒) ===")
    send({
        "action": "dumbbell_dance",
        "weight": 40,
        "duration": 8
    })
    time.sleep(9)

    # 复位
    send({
        "action": "set_servo_multi",
        "targets": {"left": 0, "right": 0},
        "speed": 60
    })
    time.sleep(1)

    # 测试 4: 轻旗子挥舞 - 快速，大幅度
    print("\n=== 测试 4: 轻旗子挥舞 (weight=15) ===")
    send({
        "action": "wave_flag",
        "channel": "left",
        "weight": 15,
        "count": 12
    })
    time.sleep(6)

    # 测试 5: 重旗子挥舞 - 慢速，小幅度
    print("\n=== 测试 5: 重旗子挥舞 (weight=70) ===")
    send({
        "action": "wave_flag",
        "channel": "right",
        "weight": 70,
        "count": 8
    })
    time.sleep(6)

    # 复位
    send({
        "action": "set_servo_multi",
        "targets": {"left": 0, "right": 0},
        "speed": 60
    })
    time.sleep(1)

    # 测试 6: 轻鼓棒打鼓 - 快速，大幅度
    print("\n=== 测试 6: 轻鼓棒打鼓 (weight=10) ===")
    send({
        "action": "beat_drum",
        "channel": "left",
        "weight": 10,
        "count": 10
    })
    time.sleep(5)

    # 测试 7: 重鼓棒打鼓 - 慢速，小幅度
    print("\n=== 测试 7: 重鼓棒打鼓 (weight=60) ===")
    send({
        "action": "beat_drum",
        "channel": "right",
        "weight": 60,
        "count": 6
    })
    time.sleep(5)

    # 复位
    send({
        "action": "set_servo_multi",
        "targets": {"left": 0, "right": 0},
        "speed": 60
    })
    time.sleep(1)

    # 测试 8: 轻桨划船 - 快速，大幅度（单手）
    print("\n=== 测试 8: 轻桨划船-单手 (weight=25) ===")
    send({
        "action": "paddle_row",
        "channel": "left",
        "weight": 25,
        "count": 5
    })
    time.sleep(8)

    # 测试 9: 重桨划船 - 慢速，小幅度（单手）
    print("\n=== 测试 9: 重桨划船-单手 (weight=75) ===")
    send({
        "action": "paddle_row",
        "channel": "right",
        "weight": 75,
        "count": 4
    })
    time.sleep(10)

    # 复位
    send({
        "action": "set_servo_multi",
        "targets": {"left": 0, "right": 0},
        "speed": 60
    })
    time.sleep(1)

    # 测试 10: 双手协同划桨 - 中等重量
    print("\n=== 测试 10: 双手协同划桨 (weight=45) ===")
    send({
        "action": "paddle_row",
        "channel": "both",
        "weight": 45,
        "count": 6
    })
    time.sleep(12)

    # 最终复位
    print("\n=== 复位所有舵机 ===")
    send({
        "action": "set_servo_multi",
        "targets": {"left": 0, "right": 0},
        "speed": 50
    })
    time.sleep(1)

    # 禁用舵机
    send({
        "action": "enable_servo",
        "channel": "both",
        "value": False
    })

    print("\n✅ 所有动作测试完成！")
    push.close()
    ctx.term()


if __name__ == "__main__":
    test_action_choreography()
