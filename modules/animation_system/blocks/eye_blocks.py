"""
眼睛动画块

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from typing import Any, Dict, Optional, List, TYPE_CHECKING
import logging

from .base_block import BaseBlock
from ..hardware_interfaces import HardwareInterfaces

if TYPE_CHECKING:
    from . import AnimationBlock

logger = logging.getLogger(__name__)


class EyeAnimationsBlock(BaseBlock):
    """眼睛动画块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('eye_animations', fields, statements)
        self.category = self.get_field('category', '')
        self.animation = self.get_field('animation', '')
        # 支持从XML读取priority，如果没有则使用默认值5
        self.priority = self.get_field('priority', None)
        if self.priority is not None:
            try:
                self.priority = int(self.priority)
            except (ValueError, TypeError):
                self.priority = 5
        else:
            self.priority = 5
        self.hold_duration = self.get_field('hold_duration', 0.0)
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行眼睛动画"""
        if not interfaces.eye:
            logger.warning("[EyeAnimationsBlock] Eye interface not available")
            return
        
        # 使用 XML 中定义的 hold_duration（默认为 0.0）
        hold = self.get_field('hold_duration', 0.0)

        priority_enabled = getattr(interfaces.eye, 'priority_enabled', True)
        logger.info(
            f"[EyeAnimationsBlock] 播放眼睛动画: category={self.category}, animation={self.animation}, "
            f"priority={self.priority}, hold={hold}, priority_enabled={priority_enabled}"
        )
        try:
            # 调用眼睛接口播放动画（不传递 wait_completion，接口自己决定）
            await interfaces.eye.play_animation(
                self.category, 
                self.animation, 
                priority=self.priority,
                hold_duration=hold
            )
            logger.debug(f"[EyeAnimationsBlock] 动画指令执行完成")
        except Exception as e:
            logger.error(f"[EyeAnimationsBlock] 播放动画失败: {e}")
            raise
    
    def validate(self) -> bool:
        """验证参数有效"""
        return bool(self.category and self.animation)


class EyeBackgroundBlock(BaseBlock):
    """设置眼睛背景块"""

    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('eye_background', fields, statements)
        # type: IMAGE or COLOR
        self.bg_type = self.get_field('type', 'IMAGE')
        self.style = self.get_field('style', None)
        # eye_side can be int (0=LEFT,1=RIGHT,2=BOTH) or string
        self.eye_side = self.get_field('eye_side', None)
        # 支持从XML读取priority，背景操作不再占用任务槽位，此字段仅用于兼容性
        self.priority = self.get_field('priority', None)
        if self.priority is not None:
            try:
                self.priority = int(self.priority)
            except (ValueError, TypeError):
                self.priority = 3  # 背景默认低优先级
        else:
            self.priority = 3
        
        # 背景持续时间（毫秒），0 表示永久
        duration_val = self.get_field('duration_ms', 0)
        self.duration_ms = int(duration_val) if duration_val is not None else 0

    async def execute(self, interfaces: HardwareInterfaces) -> None:
        if not interfaces.eye:
            logger.warning("[EyeBackgroundBlock] Eye interface not available")
            return

        # determine side string
        side = 'BOTH'
        if self.eye_side is not None:
            try:
                if isinstance(self.eye_side, int):
                    side = {0: 'LEFT', 1: 'RIGHT', 2: 'BOTH'}.get(self.eye_side, 'BOTH')
                elif isinstance(self.eye_side, str):
                    side = self.eye_side.upper()
            except Exception:
                side = 'BOTH'

        duration_str = f", duration={self.duration_ms}ms" if self.duration_ms > 0 else " (permanent)"
        logger.info(f"[EyeBackgroundBlock] set background: style={self.style} type={self.bg_type} side={side}{duration_str}")
        try:
            await interfaces.eye.set_background(self.style, bg_type=self.bg_type, side=side, duration_ms=self.duration_ms)
        except Exception as e:
            logger.error(f"[EyeBackgroundBlock] set background failed: {e}")
            raise

    def validate(self) -> bool:
        return bool(self.style)
