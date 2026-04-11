# Drive Python 测试脚本使用指南

这份文档面向第一次接触 Drive 模块测试脚本的同学，目标是让你能在不看源码细节的情况下，完成环境准备、启动服务、运行测试和修改测试。

## 1. 这些脚本能做什么

Drive 的 Python 测试脚本会通过 ZMQ 向 drive_service 发送控制命令，用来快速验证底盘、电机、舵机、RGB 灯和部分扩展 IO 是否正常工作。

目前脚本位于 libs/drive/python 目录：

- test_rgb.py：测试 RGB 灯效
- test_motor_basic.py：测试基础电机动作
- test_servo_basic.py：测试舵机动作
- test_ext_io.py：测试扩展 IO 相关动作
- test_animation_motor_api.py：测试动画系统相关的完整电机 API 场景
- drive_test_utils.py：公共测试工具，不直接执行

## 2. 运行前准备

### 2.1 编译 drive 模块

先进入 Drive 模块目录并编译：

```bash
cd /home/pi/dolydev/libs/drive
make -j2
```

### 2.2 启动 drive_service

如果还没有启动服务，可以在构建目录启动它：

```bash
cd /home/pi/dolydev/libs/drive/build
./drive_service
```

如果服务已经在运行，这一步可以跳过。

### 2.3 激活 Python 虚拟环境

执行任何 Python 测试前，先激活项目虚拟环境：

```bash
source /home/pi/dolydev/.venv/bin/activate
```

### 2.4 进入测试脚本目录

```bash
cd /home/pi/dolydev/libs/drive/python
```

## 3. 最常用的执行方式

### 3.1 运行单个快速测试

例如运行 RGB 测试：

```bash
python3 test_rgb.py
```

例如运行舵机测试：

```bash
python3 test_servo_basic.py
```

### 3.2 运行完整动画电机测试

```bash
python3 test_animation_motor_api.py
```

这个脚本会顺序执行多个场景，时间较长，测试前请确保机器人周围安全。

### 3.3 只运行动画测试中的某一个场景

先查看可用场景：

```bash
python3 test_animation_motor_api.py list
```

再运行指定场景，例如：

```bash
python3 test_animation_motor_api.py basic
python3 test_animation_motor_api.py distance
python3 test_animation_motor_api.py rotate
```

也支持数字参数：

```bash
python3 test_animation_motor_api.py 1
python3 test_animation_motor_api.py 2
python3 test_animation_motor_api.py 3
```

数字和名称的对应关系如下：

- 1 = basic
- 2 = distance
- 3 = rotate
- 4 = turn
- 5 = speed
- 6 = encoder
- 7 = pulse
- 8 = animation

## 4. 运行时你会看到什么

正常情况下，终端会输出类似信息：

```text
✅ 连接到 ipc:///tmp/doly_control.sock
🚀 Drive RGB 测试
✅ 发送: LED blink 高频闪烁
✅ 发送: LED 关闭
✅ 场景执行完成
```

含义如下：

- ✅ 连接到 ...：说明已经连上 drive_service 使用的 IPC socket
- ✅ 发送: ...：说明测试命令已经发出
- ⏳ ...：说明脚本正在等待动作执行完成
- ❌ 错误: ...：说明执行过程中出现异常

## 5. 如果运行失败，先检查这几项

### 5.1 提示连接失败或没有反应

优先检查：

- drive_service 是否已经启动
- IPC 地址 ipc:///tmp/doly_control.sock 是否由服务创建
- 是否在正确机器上运行，而不是在开发机或其他容器中运行

### 5.2 提示缺少 zmq 模块

通常是没有激活虚拟环境。先执行：

```bash
source /home/pi/dolydev/.venv/bin/activate
```

然后再重新运行测试。

### 5.3 机器人动作不符合预期

优先确认：

- 当前发送的 action 是否和服务支持的接口一致
- speed、throttle、duration、timeout_ms 参数是否合理
- 电机、舵机、RGB 硬件连接是否正常

## 6. 这次重构后，脚本结构怎么理解

现在每个测试脚本都拆成两部分：

### 6.1 公共工具

公共逻辑统一放在 libs/drive/python/drive_test_utils.py，包括：

- 建立 ZMQ 连接
- 发送命令
- 等待动作结束
- 顺序执行一组测试步骤
- 动画测试的多场景调度

### 6.2 各测试脚本只保留“测试步骤”

例如一个简单步骤长这样：

```python
command_step(
    "LED 关闭",
    {"action": "led_off"},
)
```

如果要等一段时间，可以这样写：

```python
wait_step(3.0, "等待动作完成")
```

这样做的好处是：

- 不用每个脚本都重复写 ZMQ 连接代码
- 改日志输出、连接地址、异常处理时只需要改一处
- 新增测试时更容易看懂，也更不容易写错

## 7. 怎么新增一个自己的测试脚本

最简单的方式，是参考现有脚本新建一个文件，例如 my_test.py：

```python
#!/usr/bin/env python3

from drive_test_utils import command_step, exit_with_code, run_single_scenario


STEPS = [
    command_step("设置绿色", {"action": "set_led_color", "r": 0, "g": 255, "b": 0}),
    command_step("LED 关闭", {"action": "led_off"}),
]


if __name__ == "__main__":
    exit_with_code(run_single_scenario("我的测试", STEPS))
```

保存后直接运行：

```bash
python3 my_test.py
```

## 8. 怎么修改已有测试

如果只是想调整动作顺序、参数或者等待时间，一般只需要改对应脚本中的步骤列表，不需要动公共工具。

例子：

- 想让等待更久：把 wait=1.0 改成更大的值
- 想换动作：修改 payload 里的 action 和参数
- 想增加一步：在列表里多加一个 command_step(...)

只有在下面这些情况，才需要改 drive_test_utils.py：

- 需要统一更换 socket 地址
- 需要统一修改日志输出格式
- 需要新增一种公共步骤类型

## 9. 安全提醒

运行电机相关测试前，请务必确认：

- 机器人放在平整、安全、可观察的位置
- 周围没有容易缠绕或碰撞的物体
- 有人能随时中断进程
- 完整动画测试不要在狭小桌面边缘执行

如果测试过程中动作异常，优先中断脚本，并检查 drive_service 日志。