# Doly 眼睛动画引擎 (eyeEngine)

用于控制 Doly 机器人 LCD 眼睛显示的 Python 动画引擎。

## 功能特性

- 🎨 **眼睛渲染**: 多层图像合成 (虹膜、眼睑、背景)
- 🎬 **动画播放**: 支持 .seq 格式动画文件
- 😊 **表情系统**: 预设多种表情 (开心、悲伤、生气等)
- 👀 **眼球控制**: 程序化控制瞳孔位置
- 🔄 **自动眨眼**: 可配置的自动眨眼功能
- 🔌 **集成接口**: 与 animation_system 无缝集成

## 快速开始

### 安装依赖

```bash
pip install lz4 Pillow
```

### 基本使用

```python
from eyeEngine import EyeEngine, LcdSide

# 使用上下文管理器
with EyeEngine() as engine:
    # 设置眼睛颜色
    engine.controller.set_iris_color("blue")
    
    # 设置表情
    engine.controller.set_expression("happy")
    
    # 执行眨眼
    engine.controller.blink()
    
    # 播放动画
    player = engine.play_sequence("hearts")
```

### 程序化控制

```python
from eyeEngine import EyeEngine, EyeState, LcdSide

engine = EyeEngine()
engine.init()

# 获取控制器
controller = engine.controller

# 设置虹膜位置 (范围 -1.0 到 1.0)
controller.set_iris_position(0.5, 0)  # 看向右边

# 设置颜色和主题
controller.set_iris_theme("classic", "red")

# 设置眼睑
controller.set_lid(side_id=4)  # 使用 4 号眼睑

# 设置亮度
controller.set_brightness(80)

# 释放资源
engine.release()
```

### 播放动画

```python
from eyeEngine import EyeEngine, LcdSide

with EyeEngine() as engine:
    # 播放动画 (不循环)
    player = engine.play_sequence("hearts", loop=False, fps=30)
    
    # 设置完成回调
    player.set_on_complete(lambda: print("播放完成!"))
    
    # 控制播放
    player.pause()   # 暂停
    player.resume()  # 恢复
    player.stop()    # 停止
    
    # 获取进度
    current, total = player.get_progress()
```

### 与 animation_system 集成

```python
from eyeEngine import EyeEngine
from animation_system import AnimationManager

# 初始化眼睛引擎
eye_engine = EyeEngine()
eye_engine.init()

# 获取集成接口
eye_interface = eye_engine.get_animation_interface()

# 传递给 animation_system
manager = AnimationManager()
manager.set_eye_interface(eye_interface)
```

## 目录结构

```
eyeEngine/
├── __init__.py           # 包入口
├── engine.py             # 主引擎类
├── controller.py         # 眼睛控制器
├── renderer.py           # 渲染器
├── config.py             # 配置和数据类
├── constants.py          # 常量
├── exceptions.py         # 异常类
│
├── drivers/              # 硬件驱动
│   ├── interfaces.py     # 驱动接口
│   └── lcd_driver.py     # LCD 驱动
│
├── assets/               # 资源管理
│   └── manager.py        # 资源管理器
│
├── sequence/             # 序列播放
│   ├── decoder.py        # .seq 解码器
│   └── player.py         # 播放器
│
├── integration/          # 集成接口
│   └── animation_interface.py
│
├── tests/                # 测试
├── examples/             # 示例
└── seq/                  # .seq 格式文档
```

## 配置选项

```python
from eyeEngine import EngineConfig

config = EngineConfig(
    base_path="/home/pi/.doly",        # 资源基础路径
    default_fps=30,                     # 默认帧率
    auto_blink=True,                    # 启用自动眨眼
    blink_interval=(3.0, 8.0),          # 眨眼间隔 (秒)
)

engine = EyeEngine(config)
```

## 可用表情

| 表情名称 | 描述 |
|---------|------|
| `normal` | 正常 |
| `happy` | 开心 |
| `sad` | 悲伤 |
| `angry` | 生气 |
| `surprised` | 惊讶 |
| `sleepy` | 困倦 |
| `wink_left` | 左眼眨眼 |
| `wink_right` | 右眼眨眼 |
| `look_left` | 看左边 |
| `look_right` | 看右边 |
| `look_up` | 看上面 |
| `look_down` | 看下面 |

## 资源文件

### 虹膜 (iris)
位于 `images/iris/` 目录，支持多种主题:
- classic, digi, food, glow, misc, modern, orbit, seasonal, space, symbol

每个主题包含约 20 种颜色。

### 眼睑 (lids)
位于 `images/lids/` 目录:
- 侧眼睑: 1-10, 15-18 (L/R 后缀)
- 垂直眼睑: 11-14 (T/B 后缀)

### 背景 (background)
位于 `images/background/` 目录，240x240 PNG 图片。

### 动画 (.seq)
位于 `images/animations/` 目录，LZ4 压缩的 RGBA 帧序列。

## 运行测试

```bash
cd /home/pi/.doly/libs/eyeEngine
python -m tests.run_tests

python3 /home/pi/.doly/libs/eyeEngine/examples/play_preset_animations.py --category happy

# 列出所有分类
python3 libs/eyeEngine/examples/play_preset_animations.py --list

# 播放“开心”分类的所有动画
python3 libs/eyeEngine/examples/play_preset_animations.py --category HAPPINESS



find /lib/modules/$(uname -r) -name "doly_lcd.ko"
/lib/modules/6.6.51+rpt-rpi-v8/kernel/drivers/doly/doly_lcd.ko



sudo modprobe -r doly_lcd && sudo modprobe doly_lcd
sudo fuser -k /dev/doly_lcd
lsmod | grep doly_lcd
sudo modprobe doly_lcd && ls /dev/doly_lcd

```



## 运行示例

```bash
# 基本使用
python -m examples.basic_usage

# 播放动画
python -m examples.play_animation

# 表情演示
python -m examples.expression_demo
```

## 许可

GPL-V3。

## 作者

Kevin.Liu @ Make&Share
47129927@qq.com

## 更新日志

### v1.0.0 (2026-01-27)
- 初始版本发布
