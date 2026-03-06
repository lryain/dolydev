# EyeEngine ZMQ 通信协议规范

版本：1.0  
日期：2026-01-29

## 概述

EyeEngine ZMQ 服务提供两个通信通道：

1. **命令通道**：REQ-REP 模式，客户端发送命令，服务器返回响应
2. **事件通道**：PUB-SUB 模式，服务器发布事件，客户端订阅

## 端点配置

默认端点（可通过配置文件修改）：

- 命令端点：`ipc:///tmp/doly_eye_cmd.sock`
- 事件端点：`ipc:///tmp/doly_eye_event.sock`

## 命令协议（REQ-REP）

### 请求格式

```json
{
  "action": "命令名称",
  "priority": 5,  // 可选，任务优先级 (1-10，默认 5)
  // ... 其他参数
}
```

### 响应格式

```json
{
  "success": true/false,
  "error": "错误信息",  // 仅在 success=false 时
  // ... 其他返回数据
}
```

---

## 命令列表

### 1. 测试连接

**命令**: `ping`

**请求**:
```json
{
  "action": "ping"
}
```

**响应**:
```json
{
  "success": true,
  "message": "pong"
}
```

---

### 2. 设置虹膜

**命令**: `set_iris`

**参数**:
- `theme` (string): 虹膜主题（如 "CLASSIC", "MODERN", "SPACE" 等）
- `style` (string): 虹膜样式（如 "COLOR_BLUE", "COLOR_GREEN" 等）
- `side` (string, 可选): 眼睛侧别（"LEFT", "RIGHT", "BOTH"，默认 "BOTH"）
- `priority` (int, 可选): 优先级

**请求示例**:
```json
{
  "action": "set_iris",
  "theme": "CLASSIC",
  "style": "COLOR_BLUE",
  "side": "BOTH",
  "priority": 5
}
```

**响应**:
```json
{
  "success": true
}
```

---

### 3. 设置眼睑

**命令**: `set_lid`

**参数**:
- `side_id` (int, 可选): 侧眼睑 ID
- `top_id` (int, 可选): 上眼睑 ID
- `bottom_id` (int, 可选): 下眼睑 ID
- `side` (string, 可选): 眼睛侧别（默认 "BOTH"）
- `priority` (int, 可选): 优先级

**请求示例**:
```json
{
  "action": "set_lid",
  "side_id": 1,
  "top_id": 0,
  "bottom_id": 0,
  "side": "BOTH",
  "priority": 5
}
```

**响应**:
```json
{
  "success": true
}
```

---

### 4. 设置背景

**命令**: `set_background`

**参数**:
- `style` (string): 背景样式名称
- `type` (string, 可选): 背景类型（"COLOR" 或 "IMAGE"，默认 "COLOR"）
- `priority` (int, 可选): 优先级

**请求示例**:
```json
{
  "action": "set_background",
  "style": "COLOR_BLACK",
  "type": "COLOR",
  "priority": 5
}
```

**响应**:
```json
{
  "success": true
}
```

---

### 5. 设置亮度

**命令**: `set_brightness`

**参数**:
- `level` (int): 亮度级别（0-10）
- `side` (string, 可选): 眼睛侧别（默认 "BOTH"）
- `priority` (int, 可选): 优先级

**请求示例**:
```json
{
  "action": "set_brightness",
  "level": 8,
  "side": "BOTH",
  "priority": 5
}
```

**响应**:
```json
{
  "success": true
}
```

---

### 6. 视频流控制

用于启用/禁用 FaceReco 推流在 EyeEngine 上的显示。

**命令**: `enable_video_stream`

**请求**:
```json
{
  "action": "enable_video_stream",
  "target_lcd": "RIGHT",
  "fps": 15
}
```

**响应**:
```json
{
  "success": true,
  "status": {
    "enabled": true,
    "frames": 120,
    "errors": 0,
    "fps": 14.8
  }
}
```

**命令**: `disable_video_stream`

**请求**:
```json
{
  "action": "disable_video_stream"
}
```

**响应**:
```json
{
  "success": true,
  "status": {
    "enabled": false,
    "frames": 120,
    "errors": 0,
    "fps": 0.0
  }
}
```

**命令**: `video_stream_status`

**请求**:
```json
{
  "action": "video_stream_status"
}
```

**响应**:
```json
{
  "success": true,
  "status": {
    "enabled": true,
    "frames": 120,
    "errors": 0,
    "fps": 14.8
  }
}
```

---

### 6. 播放动画

**命令**: `play_animation`

**参数**:
- `animation` (string): 动画名称（与 `id` 二选一）
- `id` (int): 动画 ID（与 `animation` 二选一）
- `fps` (int, 可选): 帧率
- `priority` (int, 可选): 优先级

**请求示例**:
```json
{
  "action": "play_animation",
  "animation": "blink1",
  "fps": 30,
  "priority": 7
}
```

或使用 ID:
```json
{
  "action": "play_animation",
  "id": 42,
  "priority": 7
}
```

**响应**:
```json
{
  "success": true,
  "task_id": "play_anim_blink1"
}
```

---

### 7. 播放行为

**命令**: `play_behavior`

**参数**:
- `behavior` (string): 行为名称（如 "ANIMATION_HAPPY"）
- `level` (int, 可选): 级别（默认 1）
- `fps` (int, 可选): 帧率
- `priority` (int, 可选): 优先级

**请求示例**:
```json
{
  "action": "play_behavior",
  "behavior": "ANIMATION_HAPPY",
  "level": 1,
  "priority": 7
}
```

**响应**:
```json
{
  "success": true,
  "task_id": "play_behavior_ANIMATION_HAPPY"
}
```

---

### 8. 眨眼

**命令**: `blink`

**参数**:
- `priority` (int, 可选): 优先级

**请求示例**:
```json
{
  "action": "blink",
  "priority": 6
}
```

**响应**:
```json
{
  "success": true
}
```

---

### 9. 停止当前任务

**命令**: `stop`

**请求**:
```json
{
  "action": "stop"
}
```

**响应**:
```json
{
  "success": true,
  "stopped": true  // 是否有任务被停止
}
```

---

### 10. 获取状态

**命令**: `get_status`

**请求**:
```json
{
  "action": "get_status"
}
```

**响应**:
```json
{
  "success": true,
  "status": {
    "current_task": "play_anim_blink1",  // 当前任务 ID，无任务时为 null
    "current_priority": 7,                // 当前任务优先级
    "is_running": true                    // 是否有任务运行
  }
}
```

---

### 11. 列出分类

**命令**: `list_categories`

**请求**:
```json
{
  "action": "list_categories"
}
```

**响应**:
```json
{
  "success": true,
  "categories": ["HAPPINESS", "SADNESS", "ANGER", ...]
}
```

---

### 12. 列出动画

**命令**: `list_animations`

**请求**:
```json
{
  "action": "list_animations"
}
```

**响应**:
```json
{
  "success": true,
  "animations": ["blink1", "blink2", "love1", ...]
}
```

---

### 13. 列出行为

**命令**: `list_behaviors`

**请求**:
```json
{
  "action": "list_behaviors"
}
```

**响应**:
```json
{
  "success": true,
  "behaviors": ["ANIMATION_HAPPY", "ANIMATION_BORED", ...]
}
```

---

### 14. 列出虹膜类型和样式

**命令**: `list_iris`

**请求**:
```json
{
  "action": "list_iris"
}
```

**响应**:
```json
{
  "success": true,
  "iris": {
    "CLASSIC": ["COLOR_BLUE", "COLOR_GREEN", ...],
    "MODERN": ["COLOR_RED", ...],
    ...
  }
}
```

---

### 15. 列出背景类型和样式

**命令**: `list_backgrounds`

**请求**:
```json
{
  "action": "list_backgrounds"
}
```

**响应**:
```json
{
  "success": true,
  "backgrounds": {
    "COLOR": ["COLOR_BLACK", "COLOR_WHITE", ...],
    "IMAGE": ["WINTER", "HEARTS", ...]
  }
}
```

---

## 事件协议（PUB-SUB）

### 事件格式

```json
{
  "type": "事件类型",
  "timestamp": 1706544000.123,  // Unix 时间戳（秒）
  "data": {
    // 事件数据
  }
}
```

### 事件类型

#### 1. 任务完成

**类型**: `task.complete`

**数据**:
```json
{
  "type": "task.complete",
  "timestamp": 1706544000.123,
  "data": {
    "task_id": "play_anim_blink1"
  }
}
```

#### 2. 任务被打断

**类型**: `task.interrupted`

**数据**:
```json
{
  "type": "task.interrupted",
  "timestamp": 1706544000.123,
  "data": {
    "old_task_id": "play_behavior_ANIMATION_HAPPY",
    "new_task_id": "play_anim_alert"
  }
}
```

---

## 优先级说明

任务优先级范围：1-10

预定义优先级：
- `10 (CRITICAL)`: 关键任务（如系统警告）
- `8 (HIGH)`: 高优先级（如用户交互）
- `5 (NORMAL)`: 普通优先级（默认）
- `3 (LOW)`: 低优先级（如自动表情）
- `1 (IDLE)`: 空闲任务（如待机动画）

**规则**：
- 高优先级任务可以打断低优先级任务
- 低优先级任务无法打断高优先级任务（请求会被拒绝）
- 同等优先级任务不会互相打断

---

## 错误码

所有错误通过响应的 `error` 字段返回，常见错误：

- `"缺少 action 字段"`: 请求未包含 action
- `"未知命令: xxx"`: 不支持的命令
- `"缺少 xxx"`: 缺少必需参数
- `"BehaviorManager 未初始化"`: 行为管理器未就绪
- 其他异常消息

---

## 使用示例

### Python 客户端示例

```python
import zmq

# 连接命令端点
ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.connect("ipc:///tmp/doly_eye_cmd.sock")

# 发送命令
cmd = {
    "action": "play_behavior",
    "behavior": "ANIMATION_HAPPY",
    "priority": 7
}
sock.send_json(cmd)

# 接收响应
response = sock.recv_json()
print(response)

sock.close()
ctx.term()
```

### 订阅事件示例

```python
import zmq

# 连接事件端点
ctx = zmq.Context()
sock = ctx.socket(zmq.SUB)
sock.connect("ipc:///tmp/doly_eye_event.sock")
sock.setsockopt_string(zmq.SUBSCRIBE, "")  # 订阅所有事件

# 接收事件
while True:
    event = sock.recv_json()
    print(f"收到事件: {event}")
```

---

## 配置文件示例

可以通过 YAML 配置文件自定义默认行为，详见 `default_config.yaml`。

启动服务时指定配置：
```bash
python zmq_service.py --config /path/to/user_config.yaml
```
