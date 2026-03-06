# Doly Animation System

一个用于解析和执行 Doly 机器人动画 XML 文件的 Python 模块。

## 特性

- ✅ 完整的 XML 动画文件解析
- ✅ 支持所有 Blockly 动画块类型
- ✅ 异步并发执行引擎
- ✅ 硬件接口抽象层
- ✅ 动画缓存机制
- ✅ 暂停/恢复/停止控制
- ✅ 易于扩展的插件架构

## 安装

将 `animation_system` 目录复制到你的项目中，或添加到 Python 路径：

```python
import sys
sys.path.insert(0, '/home/pi/.doly/libs')

from animation_system import AnimationManager, AnimationExecutor, HardwareInterfaces
```

## 快速开始

### 基本用法

```python
import asyncio
from animation_system import AnimationManager, AnimationExecutor, HardwareInterfaces
from your_project import YourEyeInterface, YourLEDInterface, ...

# 1. 创建硬件接口
interfaces = HardwareInterfaces(
    eye=YourEyeInterface(),
    led=YourLEDInterface(),
    sound=YourSoundInterface(),
    arm=YourArmInterface(),
    drive=YourDriveInterface()
)

# 2. 创建管理器和执行器
manager = AnimationManager('/home/pi/.doly/config/animations')
executor = AnimationExecutor(interfaces)

# 3. 加载动画
manager.load_animations()

# 4. 播放动画
async def play_animation():
    animation = manager.get_animation('ANIMATION_HAPPY', level=1)
    await executor.execute(animation)

asyncio.run(play_animation())
```

### 使用 Mock 接口测试

```python
from animation_system.mock_interfaces import (
    MockEyeInterface, MockLEDInterface, MockSoundInterface,
    MockArmInterface, MockDriveInterface
)

interfaces = HardwareInterfaces(
    eye=MockEyeInterface(),
    led=MockLEDInterface(),
    sound=MockSoundInterface(),
    arm=MockArmInterface(),
    drive=MockDriveInterface()
)
```

## 核心组件

### AnimationManager

管理动画资源的加载和选择。

```python
manager = AnimationManager('/path/to/animations')

# 加载动画列表
manager.load_animations()

# 获取特定动画
animation = manager.get_animation('ANIMATION_HAPPY', level=1)

# 随机选择
animation = manager.get_random_animation('ANIMATION_ANGER', level=2)

# 通过文件名获取
animation = manager.get_animation_by_file('wakeup.xml')

# 列出所有分类
categories = manager.list_categories()

# 获取分类信息
info = manager.get_category_info('ANIMATION_HAPPY')

# 清空缓存
manager.clear_cache()

# 重新加载
manager.reload_animations()
```

### AnimationExecutor

执行动画序列。

```python
executor = AnimationExecutor(interfaces)

# 执行动画
await executor.execute(animation_blocks)

# 停止动画
executor.stop()

# 暂停动画
executor.pause()

# 恢复动画
executor.resume()

# 检查状态
is_running = executor.is_running()
is_paused = executor.is_paused()
```

### HardwareInterfaces

硬件接口集合。需要实现以下接口：

- `EyeInterface`: 眼睛动画控制
- `LEDInterface`: LED 灯光控制
- `SoundInterface`: 声音播放
- `ArmInterface`: 手臂控制
- `DriveInterface`: 驱动控制

## 实现硬件接口

### 眼睛接口示例

```python
from animation_system.interfaces import EyeInterface

class MyEyeInterface(EyeInterface):
    async def play_animation(self, category: str, animation: str) -> None:
        # 实现眼睛动画播放
        print(f"Playing {category}/{animation}")
        # 调用你的眼睛动画系统
        ...
    
    async def stop_animation(self) -> None:
        # 停止当前动画
        ...
```

### LED 接口示例

```python
from animation_system.interfaces import LEDInterface

class MyLEDInterface(LEDInterface):
    async def set_color(self, color: str, side: int = 0) -> None:
        # 设置 LED 颜色
        r, g, b = self._parse_color(color)
        # 控制 LED 硬件
        ...
    
    async def set_color_with_fade(
        self,
        color: str,
        duration_ms: int,
        fade_color: str = None,
        side: int = 0
    ) -> None:
        # 实现渐变效果
        ...
    
    async def turn_off(self, side: int = 0) -> None:
        await self.set_color('#000000', side)
```

### 声音接口示例

```python
from animation_system.interfaces import SoundInterface
import pygame

class MySoundInterface(SoundInterface):
    def __init__(self):
        pygame.mixer.init()
        self._playing = False
    
    async def play(self, type_id: str, name: str, wait: bool = True) -> None:
        sound_path = f"/sounds/{type_id.lower()}/{name}.wav"
        sound = pygame.mixer.Sound(sound_path)
        sound.play()
        
        if wait:
            while pygame.mixer.get_busy():
                await asyncio.sleep(0.1)
    
    async def stop(self) -> None:
        pygame.mixer.stop()
    
    def is_playing(self) -> bool:
        return pygame.mixer.get_busy()
```

## 支持的动画块

### 控制块
- `start_animation`: 动画开始标记
- `delay_ms`: 延时
- `repeat`: 循环执行

### 表现块
- `eye_animations`: 眼睛动画
- `led`: LED 静态颜色
- `led_animation`: LED 动画序列
- `led_animation_color`: LED 颜色步骤
- `sound`: 声音播放
- `arm_set_angle`: 手臂角度设置

### 运动块
- `drive_distance`: 直线移动
- `drive_rotate_left`: 左转
- `drive_rotate_right`: 右转

## 高级功能

### 并发执行

动画块通过 `start` 字段控制执行模式：

- `start="1"`: 顺序执行，等待上一个块完成
- `start="0"`: 并行执行，与上一个块同时运行

```xml
<block type="sound">
    <field name="start">1</field>  <!-- 等待上一个块 -->
</block>
<block type="led">
    <field name="start">0</field>  <!-- 与声音同时播放 -->
</block>
```

### 子语句支持

某些块支持子语句（嵌套块）：

- `repeat`: `repeat_statement` - 循环体
- `led_animation`: `led_left`, `led_right` - 左右 LED 序列
- `sound`: `complete_statement` - 声音播放完成后执行

### 动画缓存

系统自动缓存已解析的动画文件：

```python
# 启用/禁用缓存
manager.set_cache_enabled(True)

# 清空缓存
manager.clear_cache()
```

### 扩展新的块类型

```python
from animation_system.blocks.base_block import BaseBlock
from animation_system.blocks import BlockFactory

class CustomBlock(BaseBlock):
    def __init__(self, fields):
        super().__init__('custom_block', fields)
    
    async def execute(self, interfaces):
        # 实现自定义逻辑
        ...

# 注册块类型
BlockFactory.register_block_type('custom_block', CustomBlock)
```

## 运行示例

```bash
cd /home/pi/.doly/libs
python3 animation_system/examples.py
```

示例包括：
1. 基本动画播放
2. 随机动画选择
3. 分类信息查询
4. 停止动画
5. 暂停和恢复

## 日志配置

```python
import logging

# 设置日志级别
logging.basicConfig(level=logging.DEBUG)

# 或者只配置动画系统的日志
logger = logging.getLogger('animation_system')
logger.setLevel(logging.INFO)
```

## 错误处理

系统内置错误处理机制：

- 文件不存在：记录错误，返回 None
- XML 格式错误：抛出 ValueError 异常
- 参数验证失败：跳过该块，记录警告
- 硬件接口异常：捕获并记录，不中断动画

## 性能优化

- ✅ 常用动画预加载
- ✅ 异步 I/O 操作
- ✅ 动画文件缓存
- ✅ 并发执行支持

## 文件结构

```
animation_system/
├── __init__.py              # 模块入口
├── parser.py                # XML 解析器
├── animation_manager.py     # 动画管理器
├── executor.py              # 动画执行器
├── interfaces.py            # 硬件接口定义
├── mock_interfaces.py       # Mock 接口实现
├── examples.py              # 使用示例
├── blocks/                  # 动画块实现
│   ├── __init__.py          # 块工厂
│   ├── base_block.py        # 基础类
│   ├── control_blocks.py    # 控制块
│   ├── eye_blocks.py        # 眼睛块
│   ├── led_blocks.py        # LED 块
│   ├── sound_blocks.py      # 声音块
│   ├── arm_blocks.py        # 手臂块
│   └── drive_blocks.py      # 驱动块
```

## 许可

GPL-v3

## 作者

Kevin.Liu @ Make&Share
47129927@qq.com

## 更新日志

### v1.0.0 (2026-01-27)
- 初始版本发布
- 支持所有 Blockly 动画块
- 异步执行引擎
- 完整的接口抽象层
