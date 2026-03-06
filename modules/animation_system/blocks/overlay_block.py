"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

class OverlayBlockZMQError(Exception):
    """Overlay block ZMQ 通信异常"""
    pass
"""
Overlay animation block
"""

from typing import Any, Dict, Optional, List, TYPE_CHECKING
import logging
import asyncio

from .base_block import BaseBlock
from ..hardware_interfaces import HardwareInterfaces

if TYPE_CHECKING:
    from . import AnimationBlock

logger = logging.getLogger(__name__)


class OverlayAnimationBlock(BaseBlock):
    """Overlay animation block"""

    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('overlay_animation', fields, statements)
        self.delay_ms = self.get_field('delay_ms', 0)
        self.start = self.get_field('start', 0)
        self.side = self.get_field('side', 'BOTH')

        # 自动解析 overlay_animation 子节点，支持新结构
        items = self.get_field('overlay_items', [])
        if not items:
            # 兼容新结构：直接解析子节点
            for key in ['sequence_animations', 'sprites_animations', 'text_animations']:
                val = self.get_field(key, None)
                if isinstance(val, dict) and val:
                    # 单个子块
                    t = 'sequence' if key == 'sequence_animations' else ('sprite' if key == 'sprites_animations' else 'text')
                    items.append({**val, 'type': t})
                elif isinstance(val, list):
                    t = 'sequence' if key == 'sequence_animations' else ('sprite' if key == 'sprites_animations' else 'text')
                    for v in val:
                        if isinstance(v, dict):
                            items.append({**v, 'type': t})
            # 兼容 legacy sequence_animations 顶级 dict
            legacy_seq = self.get_field('sequence_animations', {}) or {}
            if isinstance(legacy_seq, dict) and legacy_seq.get('name') and not any(i for i in items if i.get('type') == 'sequence'):
                items.append({**legacy_seq, 'type': 'sequence'})
        self.overlay_items = items

        # 兼容：如果 overlay_items 指定 side，则作为默认 side
        if isinstance(self.overlay_items, list) and self.overlay_items:
            first_side = self.overlay_items[0].get('side') if isinstance(self.overlay_items[0], dict) else None
            if first_side:
                try:
                    self.side = str(first_side).upper()
                except Exception:
                    self.side = self.get_field('side', 'BOTH')

    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """Execute overlay animation: request Eye interface to play overlay seq"""
        if not interfaces.eye:
            logger.warning("[OverlayAnimationBlock] Eye interface not available")
            return

        if not self.overlay_items:
            logger.warning("[OverlayAnimationBlock] Missing overlay items")
            return

        # optional delay applied once before first overlay
        if self.delay_ms:
            await asyncio.sleep(self.delay_ms / 1000.0)

        for overlay in self.overlay_items:
            if not isinstance(overlay, dict):
                continue

            otype = overlay.get('type', 'sequence')
            side_val = overlay.get('side', self.side)
            try:
                if isinstance(side_val, str):
                    side_str = side_val.upper()
                elif isinstance(side_val, int):
                    side_str = {0: 'LEFT', 1: 'RIGHT', 2: 'BOTH'}.get(side_val, 'BOTH')
                else:
                    side_str = 'BOTH'
            except Exception:
                side_str = 'BOTH'

            loop_flag = bool(overlay.get('loop', False))
            loop_count = overlay.get('loop_count')
            # 兼容逻辑：如果指定了 loop_count，则不应该设置 loop=True (无限循环)，而是靠 loop_count 控制
            if loop_count is not None:
                try:
                    if int(loop_count) > 0:
                        loop_flag = False
                except (ValueError, TypeError):
                    pass

            fps = overlay.get('fps', None)
            speed = float(overlay.get('speed', 1.0))
            clear_time = int(overlay.get('clear_time', 0))
            exclusive = bool(overlay.get('exclusive', False))

            if otype == 'sequence':
                seq_name = overlay.get('name')
                if not seq_name:
                    logger.warning("[OverlayAnimationBlock] Sequence overlay missing name")
                    continue
                logger.info(f"[OverlayAnimationBlock] Play overlay sequence {seq_name} side={side_str} loop={loop_flag} loop_count={loop_count} fps={fps} speed={speed} clear_time={clear_time} exclusive={exclusive}")
                try:
                    overlay_id = await interfaces.eye.play_sequence_animations(seq_name, side=side_str, loop=loop_flag, loop_count=loop_count, fps=fps, speed=speed, clear_time=clear_time, exclusive=exclusive)
                    logger.info(f"[OverlayAnimationBlock] overlay started: {overlay_id}")
                    
                    # ★ 新增：注册 overlay_id 到 AnimationIntegration（供 interrupt 使用）
                    try:
                        # 尝试通过上下文获取 animation_integration 实例
                        if hasattr(interfaces, '_animation_integration'):
                            logger.info(f"[OverlayAnimationBlock] 注册 overlay 到 animation_integration: {overlay_id} (via _animation_integration)")
                            await interfaces._animation_integration.register_overlay(overlay_id)
                        elif hasattr(interfaces, 'animation_integration'):
                            logger.info(f"[OverlayAnimationBlock] 注册 overlay 到 animation_integration: {overlay_id} (via animation_integration)")
                            await interfaces.animation_integration.register_overlay(overlay_id)
                        else:
                            logger.warning("[OverlayAnimationBlock] interfaces 上未找到 animation_integration，无法注册 overlay")
                    except Exception as e:
                        logger.warning(f"[OverlayAnimationBlock] 无法注册 overlay_id，中断功能可能受限: {e}")
                        # 显式抛出以便上层可见
                        raise
                    
                except Exception as e:
                    logger.error(f"[OverlayAnimationBlock] failed to start overlay sequence: {e}")
                    raise OverlayBlockZMQError(f"ZMQ error in overlay sequence: {e}")
            elif otype == 'sprite':
                category = overlay.get('category')
                animation = overlay.get('animation') or overlay.get('name')
                start = overlay.get('start', 0)
                # 使用上面已经解析好的 loop_flag 和 loop_count
                speed = float(overlay.get('speed', 1.0))
                clear_time = overlay.get('clear_time', 0)
                duration_ms = overlay.get('duration_ms', 0)
                delay_ms = overlay.get('delay_ms', 0)
                
                # 兼容 overlay 级别 side
                if not category or not animation:
                    logger.warning("[OverlayAnimationBlock] Sprite overlay missing category or animation")
                    continue
                if not hasattr(interfaces.eye, 'play_sprite_animation'):
                    logger.warning("[OverlayAnimationBlock] Eye interface does not support sprite animations; skipping")
                    continue
                logger.info(f"[OverlayAnimationBlock] Play sprite animation category={category} animation={animation} side={side_str} start={start} loop={loop_flag} loop_count={loop_count} speed={speed} clear_time={clear_time} duration_ms={duration_ms} delay_ms={delay_ms}")
                try:
                    overlay_id = await interfaces.eye.play_sprite_animation(
                        category, animation, start=start, loop=loop_flag, loop_count=loop_count, speed=speed, clear_time=clear_time, side=side_str
                    )
                    logger.info(f"[OverlayAnimationBlock] Sprite animation started: overlay_id={overlay_id}")
                    
                    # ★ 新增：注册 sprite overlay_id
                    try:
                        if hasattr(interfaces, '_animation_integration'):
                            logger.info(f"[OverlayAnimationBlock] 注册 sprite overlay: {overlay_id} (via _animation_integration)")
                            await interfaces._animation_integration.register_overlay(overlay_id)
                        elif hasattr(interfaces, 'animation_integration'):
                            logger.info(f"[OverlayAnimationBlock] 注册 sprite overlay: {overlay_id} (via animation_integration)")
                            await interfaces.animation_integration.register_overlay(overlay_id)
                        else:
                            logger.warning("[OverlayAnimationBlock] interfaces 上未找到 animation_integration，无法注册 sprite overlay")
                    except Exception as e:
                        logger.warning(f"[OverlayAnimationBlock] 无法注册 overlay_id，中断功能可能受限: {e}")
                    
                    # 如果指定了 duration_ms，则等待该时间
                    if duration_ms and duration_ms > 0:
                        await asyncio.sleep(duration_ms / 1000.0)
                        logger.debug(f"[OverlayAnimationBlock] Sprite animation duration_ms={duration_ms}ms completed")
                except Exception as e:
                    logger.error(f"[OverlayAnimationBlock] failed to play sprite animation: {e}")
                    raise OverlayBlockZMQError(f"ZMQ error in sprite animation: {e}")
            elif otype == 'text':
                # 占位：文本叠加尚未在 EyeEngine 实现
                logger.warning(f"[OverlayAnimationBlock] text_animations not yet supported; skipping text={overlay.get('text')}")
                continue
            else:
                logger.warning(f"[OverlayAnimationBlock] Unknown overlay item type: {otype}")

    def validate(self) -> bool:
        if self.overlay_items:
            for ov in self.overlay_items:
                if not isinstance(ov, dict):
                    continue
                otype = ov.get('type', 'sequence')
                if otype == 'sequence' and ov.get('name'):
                    return True
                if otype == 'sprite' and (ov.get('animation') or ov.get('name')):
                    return True
                if otype == 'text' and ov.get('text'):
                    return True
        seq = self.get_field('sequence_animations', None)
        return bool(seq and seq.get('name'))
