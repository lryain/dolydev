"""
精灵动画块

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

class SpritesAnimationsBlock(BaseBlock):
    """精灵动画块: 负责通过 EyeEngine 播放 assets/config/eye/eyeanimations.xml 中定义的 SpriteAnimation"""
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('sprites_animations', fields, statements)
        self.category = self.get_field('category', '')
        self.animation = self.get_field('animation', '')
        self.start = self.get_field('start', 0)

    async def execute(self, interfaces: HardwareInterfaces) -> None:
        if not interfaces.eye:
            logger.warning("[SpritesAnimationsBlock] Eye interface not available")
            return
        logger.info(f"[SpritesAnimationsBlock] 播放精灵动画: category={self.category}, animation={self.animation}, start={self.start}")
        try:
            await interfaces.eye.play_sprite_animation(
                self.category,
                self.animation,
                start=self.start
            )
            logger.debug(f"[SpritesAnimationsBlock] 精灵动画指令执行完成")
        except Exception as e:
            logger.error(f"[SpritesAnimationsBlock] 播放精灵动画失败: {e}")
            raise

    def validate(self) -> bool:
        return bool(self.category and self.animation)
