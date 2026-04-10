#!/usr/bin/env python3
"""舵机基础动作快速测试。"""

from drive_test_utils import command_step, exit_with_code, run_single_scenario


STEPS = [
    command_step("LED 红色", {"action": "set_led_color", "r": 255, "g": 0, "b": 0}),
    command_step(
        "舵机转到 10 度",
        {"action": "set_servo_multi", "targets": {"left": 10, "right": 10}, "speed": 15},
    ),
    command_step(
        "舵机转到 90 度",
        {"action": "set_servo_multi", "targets": {"left": 90, "right": 90}, "speed": 15},
    ),
    command_step(
        "舵机转到 180 度",
        {"action": "set_servo_multi", "targets": {"left": 180, "right": 180}, "speed": 15},
    ),
    command_step(
        "舵机回到 5 度",
        {"action": "set_servo_multi", "targets": {"left": 5, "right": 5}, "speed": 15},
    ),
    command_step("恢复默认指示灯", {"action": "set_led_color", "r": 0, "g": 50, "b": 0}),
    command_step("LED 关闭", {"action": "led_off"}),
]


if __name__ == "__main__":
    exit_with_code(run_single_scenario("Drive 舵机基础测试", STEPS))
