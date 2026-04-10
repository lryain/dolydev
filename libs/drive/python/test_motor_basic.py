#!/usr/bin/env python3
"""电机基础动作快速测试。"""

from drive_test_utils import command_step, exit_with_code, run_single_scenario


STEPS = [
    command_step(
        "LED 白色呼吸",
        {"action": "set_led_effect", "effect": "breath", "r": 255, "g": 255, "b": 255},
    ),
    command_step(
        "SDK 旋转 35 度",
        {"action": "drive_rotate", "angle_deg": 35.0, "is_center": False, "speed": 30, "toForward": True},
    ),
    command_step(
        "SDK 旋转 -35 度",
        {"action": "drive_rotate", "angle_deg": -35.0, "is_center": False, "speed": 30, "toForward": True},
    ),
    command_step("LED 关闭", {"action": "led_off"}),
]


if __name__ == "__main__":
    exit_with_code(run_single_scenario("Drive 电机基础测试", STEPS))
