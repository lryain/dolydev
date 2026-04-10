# 快速开始 - 重复播放功能测试

## ⚡ 5分钟快速测试

### Step 1: 启动音频播放服务

打开终端1：
```bash
cd ~/dolydev/libs/audio_player/build
./audio_player_service
```

如果看到类似日志，说明服务已启动：
```
[AudioPlayer] initialized with config: ...
[AudioPlayer] run() started
```

### Step 2: 运行测试脚本

打开终端2：
```bash
cd ~/dolydev/libs/audio_player/testing
python3 test_repeat_playback.py
```

### Step 3: 观察输出

在**终端1**应该看到类似的 debug 日志：

```
[AudioPlayer] repeat: alias=sfx_siren_2 count=1/3 interval_ms=1000
[AudioPlayer] repeat: alias=sfx_siren_2 count=2/3 interval_ms=1000
[AudioPlayer] repeat: alias=sfx_siren_2 count=3/3 interval_ms=1000
```

## 🔧 自定义测试命令

如果要手动测试，在终端2运行 Python：

```python
import zmq, json, time

ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.connect('ipc:///tmp/doly_audio_player_cmd.sock')

# 示例：播放警报声 2 次，间隔 500ms
cmd = {
    "action": "cmd.audio.play",
    "alias": "sfx_alarm",
    "playback_mode": "repeat_count",
    "play_count": 2,
    "repeat_interval_ms": 500
}

sock.send_string(json.dumps(cmd))
reply = sock.recv_string()
print("Server reply:", reply)

time.sleep(3)
sock.close()
ctx.term()
```

## 📊 支持的所有播放模式

### 单次播放（默认）
```json
{
  "action": "cmd.audio.play",
  "alias": "sfx_police",
  "priority": 5
}
```

### 重复 N 次
```json
{
  "action": "cmd.audio.play",
  "alias": "sfx_alarm",
  "playback_mode": "repeat_count",
  "play_count": 3,
  "repeat_interval_ms": 1000
}
```

### 在 M 毫秒内循环
```json
{
  "action": "cmd.audio.play",
  "alias": "sfx_clock_tick",
  "playback_mode": "repeat_duration",
  "play_duration_ms": 5000,
  "repeat_interval_ms": 200
}
```

### 停止所有播放
```json
{
  "action": "cmd.audio.stop_all"
}
```

## 🐛 故障排查

### 问题 1: "连接被拒绝" (Connection refused)
**解决**: 确保终端1中的服务正在运行
```bash
ps aux | grep audio_player_service
```

### 问题 2: 没有声音输出
**解决**: 检查音频设备配置
```bash
cat ~/dolydev/config/audio_player.yaml | grep device
```

### 问题 3: Debug 日志看不到
**解决**: 检查 stdout/stderr 重定向
- 确保 audio_player_service 在前台运行（不要用 & 后台运行）
- 或检查系统日志

## 📚 相关文档

- 完整设计文档: `libs/audio_player/docs/zmq_command_design.md`
- 实现总结: `docs/important/audio_repeat_implementation.md`
- 服务代码: `libs/audio_player/src/audio_player_service.cpp`

## ✅ 功能检查清单

- [x] 编译成功（无编译错误）
- [x] 向后兼容（单次播放仍为默认）
- [x] repeat_count 模式（支持间隔）
- [x] repeat_duration 模式（支持时长限制）
- [x] Debug 日志（实时显示播放进度）
- [x] 停止控制（支持随时停止）

---

**运维提示**: 若要在生产环境部署为 systemd 服务，参考 `install/doly-webserver.service` 的配置方式。

