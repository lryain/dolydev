"""
LED 灯光动画块

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import asyncio
from typing import Any, Dict, List, Optional, TYPE_CHECKING
import logging

from .base_block import BaseBlock
from ..hardware_interfaces import HardwareInterfaces
from ..parser import AnimationBlock

if TYPE_CHECKING:
    from . import AnimationBlock as AnimationBlockType

logger = logging.getLogger(__name__)


class LEDBlock(BaseBlock):
    """LED 静态颜色块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlockType']]] = None):
        super().__init__('led', fields, statements)
        self.color_main = self.get_field('color_main', '#000000')
        self.side = int(self.get_field('side', 0)) if self.get_field('side') is not None else 0
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """设置 LED 颜色（永久）"""
        if not interfaces.led:
            logger.warning("[LEDBlock] LED interface not available")
            return
        
        logger.info(f"[LEDBlock] 设置 LED 颜色: {self.color_main}, side={self.side}")
        try:
            await interfaces.led.set_color(self.color_main, self.side)
            logger.debug(f"[LEDBlock] 颜色设置完成: {self.color_main}")
        except Exception as e:
            logger.error(f"[LEDBlock] 设置颜色失败: {e}")
            raise
    
    def validate(self) -> bool:
        """验证参数有效"""
        return bool(self.color_main) and self.side in [0, 1, 2]


class LEDAnimationBlock(BaseBlock):
    """LED 动画序列块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['BaseBlock']]] = None):
        super().__init__('led_animation', fields, statements)
        self.led_left_blocks = statements.get('led_left', []) if statements else []
        self.led_right_blocks = statements.get('led_right', []) if statements else []
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行 LED 动画序列"""
        # 这个方法需要由执行器调用，因为需要递归执行子块
        logger.debug(f"LED animation: left={len(self.led_left_blocks)}, right={len(self.led_right_blocks)}")
    
    def get_led_left_blocks(self) -> List['BaseBlock']:
        """获取左侧 LED 动画块"""
        return self.led_left_blocks
    
    def get_led_right_blocks(self) -> List['BaseBlock']:
        """获取右侧 LED 动画块"""
        return self.led_right_blocks


class LEDAnimationColorBlock(BaseBlock):
    """LED 动画颜色块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlockType']]] = None):
        super().__init__('led_animation_color', fields, statements)
        self.color_main = self.get_field('color_main', '#000000')
        led_time_val = self.get_field('led_time', 0)
        self.led_time = int(led_time_val) if led_time_val is not None else 0
        self.color_fade = self.get_field('color_fade', None)
        self.side = 0  # 由父块控制
    
    def set_side(self, side: int) -> None:
        """设置灯光位置"""
        self.side = side
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行 LED 颜色步骤"""
        if not interfaces.led:
            logger.warning("[LEDAnimationColorBlock] LED interface not available")
            return
        
        try:
            if self.color_fade:
                # 有渐变目标颜色，使用 set_color_with_fade（会从主色渐变到淡出色）
                # logger.info(f"[LEDAnimationColorBlock] LED 颜色渐变: {self.color_main} -> {self.color_fade} "
                #            f"持续 {self.led_time}ms, side={self.side}")
                await interfaces.led.set_color_with_fade(
                    self.color_main,
                    self.led_time,
                    self.color_fade,
                    self.side
                )
            else:
                # 没有渐变色，直接设置纯色，并指定显示时长
                # logger.info(f"[LEDAnimationColorBlock] LED 纯色显示: {self.color_main} "
                #            f"持续 {self.led_time}ms, side={self.side}")
                await interfaces.led.set_color(
                    self.color_main,
                    self.side,
                    duration_ms=self.led_time
                )
            # logger.debug(f"[LEDAnimationColorBlock] LED 颜色完成")
        except Exception as e:
            logger.error(f"[LEDAnimationColorBlock] LED 动画执行失败: {e}")
            raise
    
    def get_duration(self) -> float:
        """返回颜色持续时长（秒）"""
        return self.led_time / 1000.0
    
    def validate(self) -> bool:
        """验证参数有效"""
        return bool(self.color_main) and self.led_time >= 0
