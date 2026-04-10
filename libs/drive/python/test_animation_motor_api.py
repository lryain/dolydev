#!/usr/bin/env python3
"""动画系统电机 API 完整测试。"""

import sys

from drive_test_utils import command_step, exit_with_code, run_named_scenarios, wait_step


HEADER = """
╔══════════════════════════════════════════════════════════════╗
║         动画系统电机 API 完整测试程序                        ║
║                                                              ║
║  测试前请确保:                                               ║
║    1. drive_service 已启动                                   ║
║    2. 电机已连接并正常工作                                   ║
║    3. Doly 放在安全的测试区域                                ║
║    4. 准备好观察电机运动和日志输出                           ║
╚══════════════════════════════════════════════════════════════╝
""".strip()


SCENARIOS = {
    "basic": (
        "测试1: 基础移动控制",
        [
            command_step("前进 1.5 秒", {"action": "motor_forward", "speed": 0.3, "duration": 1.5}, wait=2.0),
            command_step("后退 1.5 秒", {"action": "motor_backward", "speed": 0.3, "duration": 1.5}, wait=2.0),
            command_step("停止电机", {"action": "motor_stop"}),
        ],
    ),
    "distance": (
        "测试2: 精确距离控制",
        [
            command_step("前进 10cm", {"action": "move_distance_cm", "distance_cm": 10.0, "throttle": 0.35, "timeout_ms": 5000}, wait=4.0),
            command_step("后退 10cm", {"action": "move_distance_cm", "distance_cm": -10.0, "throttle": 0.35, "timeout_ms": 5000}, wait=4.0),
            command_step("前进 5cm", {"action": "move_distance_cm", "distance_cm": 5.0, "throttle": 0.3, "timeout_ms": 3000}, wait=3.0),
            command_step("停止电机", {"action": "motor_stop"}),
        ],
    ),
    "rotate": (
        "测试3: 精确转向控制",
        [
            command_step("右转 35 度", {"action": "turn_deg", "angle_deg": 35.0, "throttle": 0.35, "isCenter": False, "timeout_ms": 5000}),
            command_step("左转 35 度", {"action": "turn_deg", "angle_deg": -35.0, "throttle": 0.35, "isCenter": False, "timeout_ms": 5000}),
            command_step("停止电机", {"action": "motor_stop"}),
        ],
    ),
    "turn": (
        "测试4: 手动转向控制",
        [
            command_step("左转 1 秒", {"action": "motor_turn_left", "speed": 0.3, "duration": 1.0}, wait=1.5),
            command_step("右转 1 秒", {"action": "motor_turn_right", "speed": 0.3, "duration": 1.0}, wait=1.5),
            command_step("停止电机", {"action": "motor_stop"}),
        ],
    ),
    "speed": (
        "测试5: 左右轮速度控制",
        [
            command_step("双轮同速前进", {"action": "set_motor_speed", "left": 0.3, "right": 0.3, "duration": 1.0}, wait=1.5),
            command_step("右转弧线", {"action": "set_motor_speed", "left": 0.4, "right": 0.2, "duration": 1.0}, wait=1.5),
            command_step("左转弧线", {"action": "set_motor_speed", "left": 0.2, "right": 0.4, "duration": 1.0}, wait=1.5),
            command_step("原地左转", {"action": "set_motor_speed", "left": -0.3, "right": 0.3, "duration": 1.0}, wait=1.5),
            command_step("停止电机", {"action": "motor_stop"}),
        ],
    ),
    "encoder": (
        "测试6: 编码器状态查询",
        [
            command_step("查询编码器当前值", {"action": "get_encoder_values"}, wait=0.5),
            command_step("前进 10cm", {"action": "move_distance_cm", "distance_cm": 10.0, "throttle": 0.3, "timeout_ms": 5000}, wait=4.0),
            command_step("查询移动后的编码器值", {"action": "get_encoder_values"}, wait=0.5),
            command_step("停止电机", {"action": "motor_stop"}),
        ],
    ),
    "pulse": (
        "测试7: 编码器脉冲控制",
        [
            command_step("前进 100 脉冲", {"action": "motor_move_pulses", "pulses": 100, "throttle": 0.35, "assume_rate": 100.0, "timeout": 3.0}, wait=4.0),
            command_step("后退 100 脉冲", {"action": "motor_move_pulses", "pulses": -100, "throttle": 0.35, "assume_rate": 100.0, "timeout": 3.0}, wait=4.0),
            command_step("停止电机", {"action": "motor_stop"}),
        ],
    ),
    "animation": (
        "测试8: 动画场景模拟",
        [
            command_step("执行 go_forward: 前进 10cm", {"action": "move_distance_cm", "distance_cm": 10.0, "throttle": 0.3, "timeout_ms": 5000}, wait=4.0),
            command_step("执行 go_left: 左转 90 度", {"action": "turn_deg", "angle_deg": -90.0, "throttle": 0.3, "timeout_ms": 5000}, wait=4.0),
            command_step("前进 5cm", {"action": "move_distance_cm", "distance_cm": 5.0, "throttle": 0.35, "timeout_ms": 3000}),
            wait_step(10.0, "等待前进动作结束"),
            command_step("右转 45 度", {"action": "turn_deg", "angle_deg": 45.0, "throttle": 0.35, "timeout_ms": 3000}),
            wait_step(10.0, "等待转向动作结束"),
            command_step("再前进 5cm", {"action": "move_distance_cm", "distance_cm": 5.0, "throttle": 0.35, "timeout_ms": 3000}, wait=3.0),
            command_step("停止电机", {"action": "motor_stop"}),
        ],
    ),
}


ALIASES = {
    "1": "basic",
    "2": "distance",
    "3": "rotate",
    "4": "turn",
    "5": "speed",
    "6": "encoder",
    "7": "pulse",
    "8": "animation",
}


def main() -> int:
    selected = sys.argv[1] if len(sys.argv) > 1 else None
    normalized = ALIASES.get(selected, selected)
    return run_named_scenarios(HEADER, SCENARIOS, normalized)


if __name__ == "__main__":
    exit_with_code(main())
