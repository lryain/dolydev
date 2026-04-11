# ZMQ 命令扩展设计：播放次数与时长控制

## 背景
当前 `cmd.audio.play` 命令仅支持单次播放。在计时器闹钟、重复提示等场景中，需要支持：
- **按播放次数** 重复播放某个音频文件（如闹钟播放 3 次）
- **按时长** 循环播放音频直到指定时间（如闹钟响 30 秒）

## 方案设计

### 核心概念
引入 `PlaybackMode` 枚举：
- `single`：单次播放（默认，向后兼容）
- `repeat_count`：按次数重复播放
- `repeat_duration`：按时长循环播放

### 新增字段

#### `cmd.audio.play` 命令扩展
```json
{
  "action": "cmd.audio.play",
  "alias": "sfx_alarm_6",
  "uri": "file:///...",
  "priority": 60,
  "volume": 1.0,
  "ducking": false,
  "preempt": true,
  
  "playback_mode": "single|repeat_count|repeat_duration",
  "play_count": 3,                  // 仅 repeat_count 模式生效，默认 1
  "play_duration_ms": 10000,        // 仅 repeat_duration 模式生效，默认 0
  "repeat_interval_ms": 500,        // 重复间隔（两次播放之间的停顿），默认 0，单位 ms
  "repeat_fade_out_ms": 0           // 每次播放结束时的淡出时长（可选），便于平滑衔接
}
```

#### 响应格式（向后兼容）
```json
{
  "ok": true,
  "path": "assets/sounds/sfx_alarm_6.wav",
  "alias": "sfx_alarm_6",
  "priority": 60,
  
  "playback_mode": "repeat_count",
  "play_count": 3,
  "repeat_interval_ms": 500,
  "estimated_duration_ms": 4500    // 播放 3 次 + 间隔时长的估算值（可选）
}
```

### 实现细节

#### 数据结构扩展
在 `ActiveSound` 中添加：
```cpp
struct ActiveSound {
    // ...existing fields...
    
    std::string playback_mode = "single";  // single|repeat_count|repeat_duration
    int play_count = 1;                    // 需要播放的总次数
    uint64_t play_duration_ms = 0;         // 播放时长限制（ms），0 表示无限制
    uint32_t repeat_interval_ms = 0;       // 重复间隔（ms）
    uint32_t repeat_fade_out_ms = 0;       // 淡出时长（ms），默认 0
    
    // 运行时追踪
    int current_play_count = 0;            // 已播放次数
    uint64_t playback_start_ts = 0;        // 整个播放周期的开始时间（用于 repeat_duration）
    bool is_in_repeat_interval = false;    // 是否处于重复间隔中
    uint64_t interval_start_ts = 0;        // 间隔开始时间
};
```

#### 控制流程

**1. 初始播放阶段（`start_file_sound`）**
- 参数验证：若 `playback_mode` 为 `repeat_count`，则 `play_count > 0`；若为 `repeat_duration`，则 `play_duration_ms > 0`。
- 初始化 `current_play_count = 0`，`playback_start_ts = now_millis()`。
- 调用原有逻辑启动第一次播放。

**2. 更新循环（`update` 主循环每帧调用）**
检查每个 `ActiveSound`：
- 若 `playback_mode == "single"`：保持原有行为，等待 `ma_sound_is_playing()` 返回 false 即认定播放完成。
- 若 `playback_mode == "repeat_count"`：
  - 当前播放结束（`!ma_sound_is_playing()`），若 `current_play_count < play_count`：
    - 记录 `is_in_repeat_interval = true`，`interval_start_ts = now_millis()`。
    - 若 `repeat_interval_ms > 0`，等待间隔；否则立即重启。
  - 若 `current_play_count >= play_count`，标记为完成。
- 若 `playback_mode == "repeat_duration"`：
  - 当前播放结束，检查已用时 `now_millis() - playback_start_ts`：
    - 若 `< play_duration_ms`，重启播放。
    - 若 `>= play_duration_ms`，标记为完成。
  - 允许最后一次播放超期（即使播放完成时间超过限制）。

**3. 停止逻辑**
- `stop_alias("xxx")` 等仍按原有方式工作，立即停止。
- 支持新增 `cmd.audio.stop_repeat` 立即停止重复，完成当前播放但不重复。

#### 优先级与抢占
- 对于 `repeat_count` 和 `repeat_duration` 模式，`priority` 和 `preempt` 仅控制**首次播放**的抢占。
- 后续重复播放不重新评估优先级。

#### 并发预算
- 重复播放的音频只占一个并发槽位（不增加计数）。

### 迁移策略

**向后兼容**：
- 若请求中不含 `playback_mode` 或 `play_count` 等字段，默认为 `single` 模式。
- 响应中总是包含这些字段，便于客户端侦测服务端能力。

### 错误处理

| 场景 | 响应 |
|------|------|
| `play_count <= 0`（repeat_count 模式） | `{"ok": false, "error": "invalid_play_count"}` |
| `play_duration_ms <= 0`（repeat_duration 模式） | `{"ok": false, "error": "invalid_play_duration"}` |
| 文件加载失败 | `{"ok": false, "error": "cannot_start_sound"}` |

### 日志与调试
在重要节点记录日志：
1. 接收到带重复模式的命令时：记录 `playback_mode`, `play_count`, `play_duration_ms`。
2. 重复播放时：记录 `"[AudioPlayer] repeat: alias=<alias> count=<current>/<total>"`。
3. 完成重复播放时：记录总耗时。

## 配置文件扩展（可选）

在 `config/audio_player.yaml` 中可选配置默认值：
```yaml
# 重复播放默认配置
repeat_defaults:
  interval_ms: 500              # 默认重复间隔
  fade_out_ms: 100              # 默认淡出时长
  max_repeat_count: 10          # 单个命令最大重复次数限制（防止恶意）
  max_repeat_duration_ms: 300000  # 最大播放时长（5 分钟）
```

## 使用示例

### 闹钟场景（播放 3 次，间隔 1 秒）
```json
{
  "action": "cmd.audio.play",
  "alias": "sfx_alarm_6",
  "priority": 60,
  "preempt": true,
  "playback_mode": "repeat_count",
  "play_count": 3,
  "repeat_interval_ms": 1000
}
```

### 计时器结束（播放 30 秒）
```json
{
  "action": "cmd.audio.play",
  "alias": "sfx_alarm_6",
  "priority": 60,
  "playback_mode": "repeat_duration",
  "play_duration_ms": 30000
}
```

### 传统用法（向后兼容）
```json
{
  "action": "cmd.audio.play",
  "alias": "notification",
  "priority": 30
}
```
响应自动包含 `"playback_mode": "single", "play_count": 1`。

## 测试计划

### 单元测试
1. 单次播放（默认行为）
2. 重复计数模式（2、3、10 次）
3. 时长限制模式（5秒内循环）
4. 间隔控制（检验两次播放之间的停顿）
5. 提前停止（调用 `cmd.audio.stop`）

### 集成测试
见 `libs/audio_player/testing/test_repeat_playback.py`。

## 已知限制
- 暂不支持动态调整 `play_count` 或 `play_duration_ms`（需手动 stop + replay）。
- 淡出（`repeat_fade_out_ms`）目前保留接口但实现可留作后续（v2）。
