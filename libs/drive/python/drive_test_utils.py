#!/usr/bin/env python3
"""Drive Python 测试脚本共用工具。"""

from __future__ import annotations

import json
import sys
import time
from dataclasses import dataclass
from typing import Iterable, Sequence

import zmq


DEFAULT_TOPIC = "io.pca9535.control"
DEFAULT_SOCKET = "ipc:///tmp/doly_control.sock"


@dataclass(frozen=True)
class TestStep:
    kind: str
    name: str
    topic: str | None = None
    payload: dict | None = None
    wait: float = 1.0


def command_step(
    name: str,
    payload: dict,
    *,
    wait: float = 1.0,
    topic: str = DEFAULT_TOPIC,
) -> TestStep:
    return TestStep(kind="command", name=name, topic=topic, payload=payload, wait=float(wait))


def wait_step(seconds: float, name: str | None = None) -> TestStep:
    label = name or f"等待 {seconds}s"
    return TestStep(kind="wait", name=label, wait=float(seconds))


class DriveTestRunner:
    def __init__(self, socket_url: str = DEFAULT_SOCKET, connect_wait: float = 0.5):
        self.socket_url = socket_url
        self.connect_wait = float(connect_wait)
        self.ctx = zmq.Context()
        self.push = self.ctx.socket(zmq.PUSH)
        self.push.connect(self.socket_url)
        print(f"✅ 连接到 {self.socket_url}")
        time.sleep(self.connect_wait)

    def send_command(self, name: str, payload: dict, *, wait: float = 1.0, topic: str = DEFAULT_TOPIC) -> None:
        self.push.send_string(topic, zmq.SNDMORE)
        self.push.send_string(json.dumps(payload, ensure_ascii=False))
        print(f"✅ 发送: {name}")
        time.sleep(float(wait))

    def run_steps(self, steps: Iterable[TestStep]) -> None:
        for step in steps:
            if step.kind == "wait":
                print(f"⏳ {step.name}")
                time.sleep(step.wait)
                continue

            if step.kind != "command":
                raise ValueError(f"不支持的测试步骤类型: {step.kind}")

            if step.payload is None or step.topic is None:
                raise ValueError(f"命令步骤缺少 payload 或 topic: {step.name}")

            self.send_command(step.name, step.payload, wait=step.wait, topic=step.topic)

    def run_scenario(self, title: str, steps: Sequence[TestStep]) -> None:
        print(f"\n{'=' * 60}")
        print(f"🚀 {title}")
        print(f"{'=' * 60}")
        self.run_steps(steps)
        print("\n✅ 场景执行完成")

    def close(self) -> None:
        self.push.close()
        self.ctx.term()


def run_single_scenario(title: str, steps: Sequence[TestStep]) -> int:
    runner = DriveTestRunner()
    try:
        runner.run_scenario(title, steps)
        return 0
    except KeyboardInterrupt:
        print("\n❌ 测试中断")
        return 130
    except Exception as exc:
        print(f"\n❌ 错误: {exc}")
        return 1
    finally:
        runner.close()


def run_named_scenarios(header: str, scenarios: dict[str, tuple[str, Sequence[TestStep]]], selected: str | None) -> int:
    names = list(scenarios)
    requested = selected or "all"

    if requested == "list":
        print(header)
        for name, (title, _) in scenarios.items():
            print(f"  {name}: {title}")
        return 0

    if requested != "all" and requested not in scenarios:
        print(f"❌ 未知测试: {requested}")
        print(f"可用选项: all, list, {', '.join(names)}")
        return 1

    runner = DriveTestRunner()
    try:
        print(header)
        targets = names if requested == "all" else [requested]
        for name in targets:
            title, steps = scenarios[name]
            runner.run_scenario(title, steps)
        return 0
    except KeyboardInterrupt:
        print("\n❌ 测试中断")
        try:
            runner.send_command("紧急停止", {"action": "motor_stop"}, wait=0.2)
        except Exception:
            pass
        return 130
    except Exception as exc:
        print(f"\n❌ 错误: {exc}")
        try:
            runner.send_command("紧急停止", {"action": "motor_stop"}, wait=0.2)
        except Exception:
            pass
        return 1
    finally:
        runner.close()


def exit_with_code(code: int) -> None:
    raise SystemExit(code)


if __name__ == "__main__":
    print("该文件是测试工具模块，请运行具体的 test_*.py 脚本。")
    exit_with_code(0)