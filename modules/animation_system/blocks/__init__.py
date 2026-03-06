"""
动画块工厂

根据块类型创建相应的动画块对象。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from typing import Optional
import logging

from .base_block import BaseBlock, StartAnimationBlock
from .control_blocks import DelayBlock, RepeatBlock
from .eye_blocks import EyeAnimationsBlock, EyeBackgroundBlock
from .led_blocks import LEDBlock, LEDAnimationBlock, LEDAnimationColorBlock
from .sound_blocks import SoundBlock
from .tts_blocks import TTSBlock, TTSEmotionBlock
from .arm_blocks import ArmSetAngleBlock
from .drive_blocks import (
    DriveDistanceBlock, DriveForwardBlock, DriveBackwardBlock,
    DriveRotateLeftBlock, DriveRotateRightBlock,
    DriveSetAngleBlock, DriveSwingBlock, DriveSwingOfBlock,
    DriveDistancePidBlock, TurnDegPidBlock, DriveStopBlock
)
from ..parser import AnimationBlock

from .overlay_block import OverlayAnimationBlock
from .overlay_image_block import OverlayImageBlock
from .sprites_animations_block import SpritesAnimationsBlock

logger = logging.getLogger(__name__)


# 块类型映射表
BLOCK_TYPE_MAP = {
    'start_animation': StartAnimationBlock,
    'delay': DelayBlock,
    'delay_ms': DelayBlock,
    'repeat': RepeatBlock,
    'eye_animations': EyeAnimationsBlock,
    'eye_background': EyeBackgroundBlock,
    'led': LEDBlock,
    'led_animation': LEDAnimationBlock,
    'led_animation_color': LEDAnimationColorBlock,
    'sound': SoundBlock,
    'tts': TTSBlock,
    'tts_emotion': TTSEmotionBlock,
    'arm_set_angle': ArmSetAngleBlock,
    'drive_distance': DriveDistanceBlock,
    'drive_forward': DriveForwardBlock,
    'drive_backward': DriveBackwardBlock,
    'drive_rotate_left': DriveRotateLeftBlock,
    'drive_rotate_right': DriveRotateRightBlock,
    'drive_stop': DriveStopBlock,
    'drive_set_angle': DriveSetAngleBlock,
    'drive_swing': DriveSwingBlock,
    'drive_swing_of': DriveSwingOfBlock,
    'drive_distance_pid': DriveDistancePidBlock,
    'turn_deg_pid': TurnDegPidBlock,
    'overlay_animation': OverlayAnimationBlock,
    'overlay_image': OverlayImageBlock,
    'sprites_animations': SpritesAnimationsBlock,
}


class BlockFactory:
    """动画块工厂"""
    
    @staticmethod
    def create_block(animation_block: AnimationBlock) -> Optional[BaseBlock]:
        """
        根据 AnimationBlock 创建对应的 BaseBlock 实例
        
        Args:
            animation_block: 解析器生成的动画块
            
        Returns:
            动画块实例，如果类型未知则返回 None
        """
        block_type = animation_block.block_type
        block_class = BLOCK_TYPE_MAP.get(block_type)
        
        if not block_class:
            logger.warning(f"Unknown block type: {block_type}")
            return None
        
        try:
            # 递归转换 statements 中的 AnimationBlock 为 BaseBlock
            converted_statements = {}
            if animation_block.statements:
                for stmt_name, stmt_blocks in animation_block.statements.items():
                    converted_statements[stmt_name] = [
                        BlockFactory.create_block(sb) for sb in stmt_blocks
                    ]
                    # 过滤掉创建失败的块
                    converted_statements[stmt_name] = [
                        b for b in converted_statements[stmt_name] if b is not None
                    ]
            
            # 所有块都通过 statements 参数（向后兼容）
            # 为led_animation块特殊处理：需要保留led_left/led_right的结构
            if block_type == 'led_animation':
                # led_animation块期望led_left和led_right的statements
                logger.debug(f"Creating {block_type} block with LED statements")
                block = block_class(animation_block.fields, converted_statements if converted_statements else None)
            elif block_type in ['repeat', 'sound']:
                # 这些块已经支持 statements
                logger.debug(f"Creating {block_type} block with {len(converted_statements)} statements")
                block = block_class(animation_block.fields, converted_statements if converted_statements else None)
            else:
                # 其他块现在也支持 complete_statement
                logger.debug(f"Creating {block_type} block with complete_statement support")
                block = block_class(animation_block.fields, converted_statements if converted_statements else None)
            
            # 验证块
            if not block.validate():
                logger.warning(f"Block validation failed: {block_type}")
                return None
            
            if block_type == 'repeat':
                logger.info(f"[BlockFactory] 创建 repeat 块: {block.times} 次迭代, {len(block.repeat_blocks)} 个子块")
            
            return block
            
        except Exception as e:
            import traceback
            logger.error(f"Failed to create block {block_type}: {e}")
            logger.error(traceback.format_exc())
            return None
    
    @staticmethod
    def register_block_type(block_type: str, block_class):
        """
        注册新的块类型
        
        Args:
            block_type: 块类型名称
            block_class: 块类
        """
        BLOCK_TYPE_MAP[block_type] = block_class
        logger.info(f"Registered block type: {block_type}")
    
    @staticmethod
    def list_block_types():
        """列出所有支持的块类型"""
        return list(BLOCK_TYPE_MAP.keys())
