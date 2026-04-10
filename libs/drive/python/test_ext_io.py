#!/usr/bin/env python3
"""扩展 IO 快速测试。"""

from drive_test_utils import command_step, exit_with_code, run_single_scenario


STEPS = [
    command_step(
        "LED 红色闪烁",
        {"action": "set_led_effect", "effect": "blink", "r": 255, "g": 0, "b": 0, "duration_ms": 2000},
    ),
    command_step("LED 关闭", {"action": "led_off"}),
]


if __name__ == "__main__":
    exit_with_code(run_single_scenario("Drive 扩展 IO 测试", STEPS))
