#!/usr/bin/env python3
"""快速动作测试 - 验证 servo_swing_of 修复和新动作"""
import json
import time
import zmq


def quick_action_test():
    ctx = zmq.Context()
    push = ctx.socket(zmq.PUSH)
    push.connect('ipc:///tmp/doly_control.sock')
    print("✅ 连接到 ipc:///tmp/doly_control.sock")
    time.sleep(0.5)

    topic = "io.pca9535.control"

    def send(cmd):
        push.send_string(topic, zmq.SNDMORE)
        push.send_string(json.dumps(cmd))
        action_desc = cmd.get('action', 'unknown')
        if 'channel' in cmd:
            action_desc += f"[{cmd['channel']}]"
        if 'weight' in cmd:
            action_desc += f" weight={cmd['weight']}"
        print(f"👉 {action_desc}")
        time.sleep(0.1)

    # 启用舵机
    send({"action": "enable_servo", "channel": "both", "value": True})
    time.sleep(0.3)

    # 复位到初始位置
    send({"action": "set_servo_multi", "targets": {"left": 0, "right": 0}, "speed": 80})
    time.sleep(1)

    # ===== 测试 servo_swing_of 修复 =====
    print("\n【1/6】测试 servo_swing_of 修复：左手从0度移到120度，再围绕0度摆动±30度")
    send({
        "action": "servo_swing_of",
        "channel": "left",
        "target": 120,
        "approach_speed": 60,
        "amplitude": 60,  # 围绕原位置(0度)摆动±30度
        "swing_speed": 70,
        "count": 5
    })
    time.sleep(5)

    # 复位
    send({"action": "set_servo_multi", "targets": {"left": 0, "right": 0}, "speed": 70})
    time.sleep(1)

    # ===== 测试举哑铃 =====
    print("\n【2/6】轻哑铃(20) vs 重哑铃(80)：观察速度和幅度差异")
    send({"action": "lift_dumbbell", "channel": "left", "weight": 20, "reps": 3})
    time.sleep(5)
    
    send({"action": "lift_dumbbell", "channel": "right", "weight": 80, "reps": 2})
    time.sleep(6)

    # 复位
    send({"action": "set_servo_multi", "targets": {"left": 0, "right": 0}, "speed": 70})
    time.sleep(1)

    # ===== 测试双手举哑铃舞蹈 =====
    print("\n【3/6】双手交替举哑铃舞蹈 (weight=35, 6秒)")
    send({"action": "dumbbell_dance", "weight": 35, "duration": 6})
    time.sleep(7)

    # 复位
    send({"action": "set_servo_multi", "targets": {"left": 0, "right": 0}, "speed": 70})
    time.sleep(1)

    # ===== 测试挥旗 =====
    print("\n【4/6】挥旗：轻旗(15)大幅快速 vs 重旗(65)小幅慢速")
    send({"action": "wave_flag", "channel": "left", "weight": 15, "count": 8})
    time.sleep(4)
    
    send({"action": "wave_flag", "channel": "right", "weight": 65, "count": 6})
    time.sleep(5)

    # 复位
    send({"action": "set_servo_multi", "targets": {"left": 0, "right": 0}, "speed": 70})
    time.sleep(1)

    # ===== 测试打鼓 =====
    print("\n【5/6】打鼓：轻鼓棒(12)快速 vs 重鼓棒(55)慢速")
    send({"action": "beat_drum", "channel": "left", "weight": 12, "count": 8})
    time.sleep(4)
    
    send({"action": "beat_drum", "channel": "right", "weight": 55, "count": 5})
    time.sleep(4)

    # 复位
    send({"action": "set_servo_multi", "targets": {"left": 0, "right": 0}, "speed": 70})
    time.sleep(1)

    # ===== 测试划桨 =====
    print("\n【6/6】划桨：双手协同 (weight=40)")
    send({"action": "paddle_row", "channel": "both", "weight": 40, "count": 4})
    time.sleep(8)

    # 最终复位
    print("\n=== 复位并禁用舵机 ===")
    send({"action": "set_servo_multi", "targets": {"left": 0, "right": 0}, "speed": 50})
    time.sleep(1)
    send({"action": "enable_servo", "channel": "both", "value": False})

    print("\n✅ 快速测试完成！\n")
    print("【验证要点】")
    print("  1. servo_swing_of: 移动到目标后，应围绕原始角度摆动（不是目标角度）")
    print("  2. 举哑铃: 重量越大 → 速度越慢、幅度越小")
    print("  3. 挥旗: 重量影响挥舞速度和幅度")
    print("  4. 打鼓: 重量影响击打速度和幅度")
    print("  5. 划桨: 拉桨慢（用力），回桨快（放松）")
    
    push.close()
    ctx.term()


if __name__ == "__main__":
    quick_action_test()
