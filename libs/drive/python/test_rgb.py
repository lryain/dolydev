#!/usr/bin/env python3
"""RGB 灯效快速测试。"""

from drive_test_utils import command_step, exit_with_code, run_single_scenario


STEPS = [
    command_step(
        "LED blink 高频闪烁",
        {
            "action": "set_led_effect",
            "effect": "blink",
            "r": 255,
            "g": 40,
            "b": 40,
            "frequency_hz": 10.0,
            "duration_ms": 3000,
        },
        wait=3.5,
    ),
    command_step("LED 关闭", {"action": "led_off"}),
]


if __name__ == "__main__":
    exit_with_code(run_single_scenario("Drive RGB 测试", STEPS))
