# EyeEngine ZMQ 服务使用指南

## 概述

eyeEngine ZMQ 服务提供了完整的眼睛动画控制接口，支持：

- ✅ 配置文件驱动的默认参数
- ✅ 任务优先级管理（高优先级任务可打断低优先级任务）
- ✅ ZMQ 协议控制所有功能
- ✅ 事件发布（任务完成、打断等）
- ✅ 自动眨眼和表情轮播
- ✅ 完整的测试客户端

## 快速开始

### 1. 启动服务

```bash
# 使用默认配置
cd /home/pi/.doly/libs/eyeEngine
python3 zmq_service.py

# 使用自定义配置
python3 zmq_service.py --config my_config.yaml

# 使用模拟驱动（调试用）
python3 zmq_service.py --mock

# 调试模式
python3 zmq_service.py --log-level DEBUG
```

### 2. 测试客户端

```bash
# 运行所有测试
python3 test_client.py --test all

# 运行特定测试
python3 test_client.py --test ping
python3 test_client.py --test list
python3 test_client.py --test behavior
python3 test_client.py --test priority  # 测试优先级打断

# 交互模式
python3 test_client.py --interactive
```

## 配置文件

### 默认配置文件：`default_config.yaml`

```yaml
# 眼睛外观配置
appearance:
  iris_theme: "CLASSIC"
  iris_style: "COLOR_BLUE"
  side_lid_id: 0
  top_lid_id: 0
  bottom_lid_id: 0
  background_type: "COLOR"
  background_style: "COLOR_BLACK"
  brightness: 8

# 自动眨眼
auto_blink:
  enabled: true
  interval_min: 2.0
  interval_max: 6.0

# 自动表情轮播
auto_expression_carousel:
  enabled: false
  expressions:
    - "ANIMATION_HAPPY"
    - "ANIMATION_BORED"
  duration: 5.0
  interval: 2.0
  random_order: true

# 任务优先级
task_priority:
  enabled: true
  default_priority: 5
  max_priority: 10
  min_priority: 1

# ZMQ 端点
zmq_service:
  command_endpoint: "ipc:///tmp/doly_eye_cmd.sock"
  event_endpoint: "ipc:///tmp/doly_eye_event.sock"
```

### 自定义配置

创建 `my_config.yaml`，只需包含要覆盖的项：

```yaml
appearance:
  iris_style: "COLOR_GREEN"
  brightness: 10

auto_expression_carousel:
  enabled: true
```

## ZMQ 协议

详细协议文档见：`ZMQ_PROTOCOL.md`

### 常用命令示例

#### Python 客户端

```python
import zmq
import json

ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.connect("ipc:///tmp/doly_eye_cmd.sock")

# 设置虹膜
cmd = {
    "action": "set_iris",
    "theme": "CLASSIC",
    "style": "COLOR_BLUE",
    "priority": 5
}
sock.send_json(cmd)
response = sock.recv_json()
print(response)

# 播放行为
cmd = {
    "action": "play_behavior",
    "behavior": "ANIMATION_HAPPY",
    "priority": 7
}
sock.send_json(cmd)
response = sock.recv_json()
print(response)

sock.close()
```

#### Bash 客户端（使用 zmqc）

```bash
# 安装 zmqc
pip install zmqc

# 发送命令
echo '{"action":"ping"}' | zmqc -c req ipc:///tmp/doly_eye_cmd.sock

# 播放行为
echo '{"action":"play_behavior","behavior":"ANIMATION_HAPPY","priority":7}' | \
  zmqc -c req ipc:///tmp/doly_eye_cmd.sock
```

## 任务优先级

### 优先级级别

- `10 (CRITICAL)`: 关键任务（系统警告）
- `8 (HIGH)`: 高优先级（用户交互）
- `5 (NORMAL)`: 普通优先级（默认）
- `3 (LOW)`: 低优先级（自动表情）
- `1 (IDLE)`: 空闲任务（待机动画）

### 打断规则

- ✅ 高优先级任务 **可以** 打断低优先级任务
- ❌ 低优先级任务 **无法** 打断高优先级任务（请求被拒绝）
- ❌ 同等优先级任务 **不会** 互相打断

### 示例

```python
# 1. 启动低优先级任务（自动表情）
cmd = {
    "action": "play_behavior",
    "behavior": "ANIMATION_BORED",
    "priority": 3  # LOW
}
sock.send_json(cmd)

# 2. 用户点击后，播放高优先级反馈（会打断上面的任务）
cmd = {
    "action": "play_behavior",
    "behavior": "ANIMATION_HAPPY",
    "priority": 8  # HIGH
}
sock.send_json(cmd)
# 响应: {"success": true, "task_id": "play_behavior_ANIMATION_HAPPY"}

# 3. 订阅事件，会收到打断通知
# 事件: {"type": "task.interrupted", "data": {"old_task_id": "...", "new_task_id": "..."}}
```

## 事件订阅

```python
import zmq

ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("ipc:///tmp/doly_eye_event.sock")
sock.setsockopt_string(zmq.SUBSCRIBE, "")  # 订阅所有事件

while True:
    event = sock.recv_json()
    print(f"事件类型: {event['type']}")
    print(f"事件数据: {event['data']}")
    
    # 处理不同事件
    if event['type'] == 'task.complete':
        task_id = event['data']['task_id']
        print(f"任务完成: {task_id}")
    
    elif event['type'] == 'task.interrupted':
        old = event['data']['old_task_id']
        new = event['data']['new_task_id']
        print(f"任务打断: {old} -> {new}")
```

## 功能清单

### 已实现

- ✅ **配置文件系统**
  - `default_config.yaml` - 默认配置
  - `config_loader_v2.py` - 配置加载器
  - 支持用户配置覆盖

- ✅ **任务优先级管理**
  - `task_priority.py` - 优先级管理器
  - 支持 1-10 优先级
  - 自动打断低优先级任务
  - 任务完成/打断事件

- ✅ **ZMQ 服务**
  - `zmq_service.py` - 服务入口
  - REQ-REP 命令通道
  - PUB-SUB 事件通道
  - 15+ 命令支持

- ✅ **测试客户端**
  - `test_client.py` - 完整测试客户端
  - 支持所有命令测试
  - 交互模式
  - 优先级打断测试

- ✅ **文档**
  - `ZMQ_PROTOCOL.md` - 完整协议文档
  - `USAGE.md` - 使用指南（本文档）

## 常见问题

### Q: 如何修改默认虹膜颜色？

A: 编辑 `default_config.yaml` 或创建自定义配置：

```yaml
appearance:
  iris_theme: "CLASSIC"
  iris_style: "COLOR_GREEN"  # 改为绿色
```

### Q: 如何启用自动表情轮播？

A: 在配置文件中启用：

```yaml
auto_expression_carousel:
  enabled: true
  expressions:
    - "ANIMATION_HAPPY"
    - "ANIMATION_BORED"
    - "ANIMATION_THINKING"
  duration: 5.0
  interval: 2.0
  random_order: true
```

### Q: 如何确保重要任务不被打断？

A: 使用高优先级（8-10）：

```python
cmd = {
    "action": "play_behavior",
    "behavior": "SYSTEM_ALERT",
    "priority": 10  # CRITICAL - 不会被打断
}
```

### Q: 可以同时控制左右眼不同颜色吗？

A: 可以：

```python
# 左眼蓝色
cmd = {"action": "set_iris", "theme": "CLASSIC", "style": "COLOR_BLUE", "side": "LEFT"}
sock.send_json(cmd)

# 右眼绿色
cmd = {"action": "set_iris", "theme": "CLASSIC", "style": "COLOR_GREEN", "side": "RIGHT"}
sock.send_json(cmd)
```

## 集成示例

### 与其他模块协作

```python
# 示例：语音识别触发表情
import zmq

ctx = zmq.Context()
eye_cmd = ctx.socket(zmq.REQ)
eye_cmd.connect("ipc:///tmp/doly_eye_cmd.sock")

def on_speech_detected(text):
    """语音识别回调"""
    if "高兴" in text:
        cmd = {
            "action": "play_behavior",
            "behavior": "ANIMATION_HAPPY",
            "priority": 8  # 用户交互优先级
        }
        eye_cmd.send_json(cmd)
        response = eye_cmd.recv_json()
```
