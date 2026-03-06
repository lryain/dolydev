# eyeEngine ZMQ API 完整文档

## 1. API 分类总览

eyeEngine 提供了 36+ 个 ZMQ 命令接口，分为以下几类：

### 1.1 配置类 (Configuration)
- `set_iris` - 设置虹膜主题和样式
- `set_lid` - 设置眼睑样式
- `set_background` - 设置背景
- `set_brightness` - 设置亮度

### 1.2 动画播放类 (Animation Playback)
- `play_animation` - 播放眼睛动画
- `play_behavior` - 播放行为动画
- `play_category` - 播放分类动画
- `play_all_animations` - 播放所有动画
- `play_sequence` - 播放序列动画
- `play_sprite_animation` - 播放精灵动画

### 1.3 叠加层类 (Overlay)
- `play_sequence_animations` - 播放序列动画叠加层
- `play_overlay_sequence_sync` - 同步播放序列叠加层
- `stop_overlay_sequence` - 停止序列叠加层
- `stop_overlay_sequence_sync` - 同步停止序列叠加层
- `play_overlay_image` - 播放图片叠加层
- `play_overlay_image_sync` - 同步播放图片叠加层
- `play_text_overlay` - 播放文字叠加层
- `stop_overlay_image` - 停止图片叠加层
- `stop_overlay_image_sync` - 同步停止图片叠加层

### 1.4 眼睛动作类 (Eye Actions)
- `blink` - 眨眼

### 1.5 控制类 (Control)
- `stop` - 停止当前动画
- `set_priority_enabled` - 设置优先级系统开关

### 1.6 查询类 (Query)
- `list_sequences` - 列出所有序列动画
- `list_behaviors` - 列出所有行为动画
- `list_iris` - 列出所有虹膜样式
- `list_backgrounds` - 列出所有背景样式
- `ping` - 心跳检测

### 1.7 可见性管理类 (Visibility Management)
- `show_widget` - 显示 widget（隐藏眼睛）
- `restore_eye` - 恢复眼睛显示
- `pause_auto_restore` - 暂停自动恢复
- `resume_auto_restore` - 恢复自动恢复
- `set_manual_mode` - 设置手动模式
- `get_visibility_status` - 获取可见性状态

### 1.8 视频流类 (Video Stream)
- `enable_video_stream` - 启用视频流
- `disable_video_stream` - 禁用视频流
- `video_stream_status` - 获取视频流状态

### 1.9 调试类 (Debug)
- `debug_fail` - 调试失败场景

---

## 2. 详细 API 说明

### 2.1 配置类

#### 2.1.1 set_iris
**功能**: 设置虹膜主题和样式

**参数**:
```json
{
  "action": "set_iris",
  "theme": "默认主题",      // 虹膜主题名称
  "style": "normal",       // 虹膜样式
  "side": "BOTH"          // LEFT/RIGHT/BOTH
}
```

**响应**:
```json
{
  "success": true,
  "task_id": "set_iris_BOTH_默认主题_normal"
}
```

**特点**: 瞬时执行，不占用任务槽位

---

#### 2.1.2 set_lid
**功能**: 设置眼睑样式

**参数**:
```json
{
  "action": "set_lid",
  "side_id": "default",    // 眼睑侧边ID（可选）
  "top_id": "default",     // 上眼睑ID（可选）
  "bottom_id": "default",  // 下眼睑ID（可选）
  "side": "BOTH"          // LEFT/RIGHT/BOTH
}
```

**响应**:
```json
{
  "success": true,
  "task_id": "set_lid_BOTH"
}
```

**特点**: 瞬时执行，不占用任务槽位

---

#### 2.1.3 set_background
**功能**: 设置背景

**参数**:
```json
{
  "action": "set_background",
  "style": "black",        // 背景样式名称
  "type": "COLOR",         // COLOR/IMAGE
  "side": "BOTH",         // LEFT/RIGHT/BOTH
  "duration_ms": 0        // 持续时间（0=永久）
}
```

**响应**:
```json
{
  "success": true,
  "task_id": "set_background_BOTH_black"
}
```

**特点**: 瞬时执行，支持自动清除

---

#### 2.1.4 set_brightness
**功能**: 设置屏幕亮度

**参数**:
```json
{
  "action": "set_brightness",
  "brightness": 128,       // 0-255
  "side": "BOTH"          // LEFT/RIGHT/BOTH
}
```

**响应**:
```json
{
  "success": true,
  "task_id": "set_brightness_BOTH_128"
}
```

**特点**: 瞬时执行

---

### 2.2 动画播放类

#### 2.2.1 play_animation
**功能**: 播放 eyeanimations.xml 中定义的眼睛动画

**参数**:
```json
{
  "action": "play_animation",
  "category": "HAPPY",      // 动画分类
  "animation": "happy_1",   // 动画名称或ID
  "priority": 5,            // 优先级 1-10
  "hold_duration": 0.0      // 保持时长（秒）
}
```

**响应**:
```json
{
  "success": true,
  "task_id": "anim_HAPPY_happy_1_xxxxx"
}
```

**特点**: 
- 支持优先级队列
- 支持 hold_duration 保持表情

---

#### 2.2.2 play_behavior
**功能**: 播放行为动画（从 animationlist.xml）

**参数**:
```json
{
  "action": "play_behavior",
  "behavior": "happy",      // 行为名称
  "level": 1,              // 分类等级
  "priority": 5,
  "hold_duration": 0.0
}
```

**响应**:
```json
{
  "success": true,
  "task_id": "behavior_happy_1_xxxxx"
}
```

---

#### 2.2.3 play_category
**功能**: 播放指定分类的动画

**参数**:
```json
{
  "action": "play_category",
  "category": "HAPPY",      // 分类名称
  "priority": 5
}
```

**响应**:
```json
{
  "success": true,
  "task_id": "category_HAPPY_xxxxx"
}
```

---

#### 2.2.4 play_sprite_animation
**功能**: 播放精灵动画

**参数**:
```json
{
  "action": "play_sprite_animation",
  "category": "SPRITE_FRUIT",   // 精灵分类
  "animation": "apple",         // 精灵名称
  "start": 0,                   // 起始帧
  "loop": false,                // 是否循环
  "loop_count": 0,              // 循环次数（0=无限）
  "speed": 1.0,                 // 播放速度
  "clear_time": 0,              // 结束后清屏时间(ms)
  "side": "BOTH"               // LEFT/RIGHT/BOTH
}
```

**响应**:
```json
{
  "success": true,
  "overlay_id": "sprite_apple_xxxxx",
  "task_id": "sprite_apple_xxxxx"
}
```

---

### 2.3 叠加层类

#### 2.3.1 play_sequence_animations (play_overlay_sequence)
**功能**: 在眼睛上叠加播放 .seq 动画

**参数**:
```json
{
  "action": "play_sequence_animations",
  "sequence": "test.seq",   // .seq 文件名
  "side": "BOTH",          // LEFT/RIGHT/BOTH
  "loop": false,
  "loop_count": 0,
  "fps": 30,               // 帧率（可选）
  "speed": 1.0,            // 播放速度
  "delay_ms": 0,           // 延迟启动(ms)
  "clear_time": 0,         // 结束后清屏时间(ms)
  "exclusive": false       // 是否独占显示
}
```

**响应**:
```json
{
  "success": true,
  "overlay_id": "seq_test_xxxxx",
  "task_id": "seq_test_xxxxx"
}
```

---

#### 2.3.2 play_overlay_image
**功能**: 在眼睛上叠加显示图片

**参数**:
```json
{
  "action": "play_overlay_image",
  "image": "test.png",      // 图片文件名
  "side": "BOTH",
  "x": 0,                   // X 偏移
  "y": 0,                   // Y 偏移
  "scale": 1.0,             // 缩放
  "rotation": 0.0,          // 旋转角度
  "duration_ms": 0,         // 持续时间（0=永久）
  "delay_ms": 0             // 延迟启动
}
```

**响应**:
```json
{
  "success": true,
  "overlay_id": "img_test_xxxxx"
}
```

---

#### 2.3.3 play_text_overlay
**功能**: 在眼睛上叠加显示文字

**参数**:
```json
{
  "action": "play_text_overlay",
  "text": "Hello Doly",     // 文字内容
  "side": "BOTH",
  "x": 120,                 // X 坐标
  "y": 120,                 // Y 坐标
  "font_size": 24,          // 字体大小
  "color": "#FFFFFF",       // 文字颜色
  "duration_ms": 3000       // 持续时间
}
```

**响应**:
```json
{
  "success": true,
  "overlay_id": "text_xxxxx"
}
```

---

#### 2.3.4 stop_overlay_sequence_sync
**功能**: 同步停止序列叠加层

**参数**:
```json
{
  "action": "stop_overlay_sequence_sync",
  "overlay_id": "seq_test_xxxxx"
}
```

**响应**:
```json
{
  "success": true
}
```

---

#### 2.3.5 stop_overlay_image_sync
**功能**: 同步停止图片叠加层

**参数**:
```json
{
  "action": "stop_overlay_image_sync",
  "overlay_id": "img_test_xxxxx"
}
```

**响应**:
```json
{
  "success": true
}
```

---

### 2.4 眼睛动作类

#### 2.4.1 blink
**功能**: 眨眼

**参数**:
```json
{
  "action": "blink",
  "count": 1,               // 眨眼次数
  "side": "BOTH",          // LEFT/RIGHT/BOTH
  "duration": 200          // 每次眨眼持续时间(ms)
}
```

**响应**:
```json
{
  "success": true,
  "task_id": "blink_xxxxx"
}
```

**注意**: 
- 当前实现可能没有完整支持所有参数
- 需要检查 `_cmd_blink()` 实现

---

### 2.5 控制类

#### 2.5.1 stop
**功能**: 停止当前动画（清空任务队列）

**参数**:
```json
{
  "action": "stop"
}
```

**响应**:
```json
{
  "success": true,
  "message": "已停止所有任务"
}
```

---

#### 2.5.2 set_priority_enabled
**功能**: 启用/禁用优先级系统

**参数**:
```json
{
  "action": "set_priority_enabled",
  "enabled": true           // true=启用, false=禁用
}
```

**响应**:
```json
{
  "success": true,
  "enabled": true,
  "status": {
    "enabled": true,
    "current": null,
    "pending": []
  }
}
```

---

### 2.6 查询类

#### 2.6.1 list_sequences
**功能**: 列出所有可用的序列动画

**参数**:
```json
{
  "action": "list_sequences"
}
```

**响应**:
```json
{
  "success": true,
  "sequences": ["test.seq", "animation1.seq", ...]
}
```

---

#### 2.6.2 list_behaviors
**功能**: 列出所有行为动画

**参数**:
```json
{
  "action": "list_behaviors"
}
```

**响应**:
```json
{
  "success": true,
  "behaviors": [
    {"name": "happy", "level": 1},
    {"name": "sad", "level": 2},
    ...
  ]
}
```

---

#### 2.6.3 list_iris
**功能**: 列出所有虹膜样式

**参数**:
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
    "默认主题": ["normal", "cat", ...],
    ...
  }
}
```

---

#### 2.6.4 list_backgrounds
**功能**: 列出所有背景样式

**参数**:
```json
{
  "action": "list_backgrounds"
}
```

**响应**:
```json
{
  "success": true,
  "backgrounds": ["black", "white", "blue", ...]
}
```

---

#### 2.6.5 ping
**功能**: 心跳检测

**参数**:
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

### 2.7 可见性管理类

#### 2.7.1 show_widget
**功能**: 显示 widget，隐藏眼睛

**参数**:
```json
{
  "action": "show_widget",
  "widget_id": "clock"      // widget ID
}
```

**响应**:
```json
{
  "success": true
}
```

---

#### 2.7.2 restore_eye
**功能**: 恢复眼睛显示

**参数**:
```json
{
  "action": "restore_eye"
}
```

**响应**:
```json
{
  "success": true
}
```

---

#### 2.7.3 get_visibility_status
**功能**: 获取当前可见性状态

**参数**:
```json
{
  "action": "get_visibility_status"
}
```

**响应**:
```json
{
  "success": true,
  "eye_visible": true,
  "widget_visible": false,
  "auto_restore_enabled": true
}
```

---

### 2.8 视频流类

#### 2.8.1 enable_video_stream
**功能**: 启用摄像头视频流叠加

**参数**:
```json
{
  "action": "enable_video_stream",
  "resource_id": "facereco_video"
}
```

**响应**:
```json
{
  "success": true
}
```

---

#### 2.8.2 disable_video_stream
**功能**: 禁用视频流

**参数**:
```json
{
  "action": "disable_video_stream"
}
```

**响应**:
```json
{
  "success": true
}
```

---

#### 2.8.3 video_stream_status
**功能**: 获取视频流状态

**参数**:
```json
{
  "action": "video_stream_status"
}
```

**响应**:
```json
{
  "success": true,
  "enabled": true,
  "resource_id": "facereco_video"
}
```
---
