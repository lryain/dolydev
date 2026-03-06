"""
Overlay image block

Plays a PNG image overlay on the eyes, optionally with simple animation (linear interpolation
between start/end transforms over duration).

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from typing import Any, Dict, Optional, List, TYPE_CHECKING
import logging
import asyncio

from .base_block import BaseBlock
from ..hardware_interfaces import HardwareInterfaces

if TYPE_CHECKING:
    from . import AnimationBlock

logger = logging.getLogger(__name__)


class OverlayImageBlock(BaseBlock):
    """Overlay image block"""

    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('overlay_image', fields, statements)
        # Required: image name or path
        self.image = self.get_field('image', None)
        self.side = self.get_field('side', 'BOTH')
        self.loop = bool(self.get_field('loop', False))
        self.fps = self.get_field('fps', None)
        self.delay_ms = int(self.get_field('delay_ms', 0) or 0)
        # transforms: relative to center in pixels
        self.x = int(self.get_field('x', 0) or 0)
        self.y = int(self.get_field('y', 0) or 0)
        self.scale = float(self.get_field('scale', 1.0) or 1.0)
        self.rotation = float(self.get_field('rotation', 0.0) or 0.0)
        # optional end transforms for simple animation
        self.x_end = self.get_field('x_end', None)
        self.y_end = self.get_field('y_end', None)
        self.scale_end = self.get_field('scale_end', None)
        self.rotation_end = self.get_field('rotation_end', None)
        # total duration in ms
        self.duration_ms = int(self.get_field('duration_ms', 0) or 0) or None

    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """Start overlay image playback via Eye interface"""
        if not interfaces.eye:
            logger.warning("[OverlayImageBlock] Eye interface not available")
            return

        if not self.image:
            logger.warning("[OverlayImageBlock] Missing image field")
            return

        # optional delay before starting
        if self.delay_ms:
            logger.debug(f"[OverlayImageBlock] delaying overlay image {self.image} by {self.delay_ms}ms")
            await asyncio.sleep(self.delay_ms / 1000.0)

        params = {
            'image': self.image,
            'side': self.side,
            'loop': self.loop,
            'fps': self.fps,
            'x': self.x,
            'y': self.y,
            'scale': self.scale,
            'rotation': self.rotation,
            'duration_ms': self.duration_ms,
            'delay_ms': 0
        }

        # include optional end transforms
        if self.x_end is not None: params['x_end'] = int(self.x_end)
        if self.y_end is not None: params['y_end'] = int(self.y_end)
        if self.scale_end is not None: params['scale_end'] = float(self.scale_end)
        if self.rotation_end is not None: params['rotation_end'] = float(self.rotation_end)

        logger.info(f"[OverlayImageBlock] play image overlay {self.image} side={self.side} params={params}")
        try:
            overlay_id = await interfaces.eye.play_overlay_image(
                image=self.image,
                side=self.side,
                loop=self.loop,
                fps=self.fps,
                x=self.x,
                y=self.y,
                scale=self.scale,
                rotation=self.rotation,
                duration_ms=self.duration_ms,
                delay_ms=self.delay_ms
            )
            logger.info(f"[OverlayImageBlock] overlay started: {overlay_id}")
        except Exception as e:
            logger.error(f"[OverlayImageBlock] failed to start overlay image: {e}")
            raise

    def validate(self) -> bool:
        return bool(self.image)
