"""
声音播放块

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from typing import Any, Dict, List, Optional
import logging

from .base_block import BaseBlock
from ..hardware_interfaces import HardwareInterfaces
from ..parser import AnimationBlock

logger = logging.getLogger(__name__)


class SoundBlock(BaseBlock):
    """声音播放块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['BaseBlock']]] = None):
        super().__init__('sound', fields, statements)
        self.type_id = self.get_field('type_id', '')
        self.name = self.get_field('name', '')
        self.wait = self.get_field('wait', True)  # 是否等待播放完成
        self.delay_ms = self.get_field('delay_ms', 0)
        statements = statements or {}
        self.complete_blocks = statements.get('complete_statement', [])
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """播放声音"""
        if not interfaces.sound:
            logger.warning("[SoundBlock] Sound interface not available")
            return
        
        logger.info(f"[SoundBlock] 播放音频: {self.type_id}/{self.name}, wait={self.wait}")
        # optional pre-play delay
        if self.delay_ms:
            import asyncio as _asyncio
            logger.debug(f"[SoundBlock] delaying sound {self.name} by {self.delay_ms}ms")
            await _asyncio.sleep(self.delay_ms / 1000.0)
        try:
            await interfaces.sound.play(self.type_id, self.name, wait=self.wait)
            logger.debug(f"[SoundBlock] 音频播放完成: {self.type_id}/{self.name}")
        except Exception as e:
            logger.error(f"[SoundBlock] 播放音频失败: {e}")
            raise
    
    def get_type_id(self) -> str:
        """获取声音类型"""
        return self.type_id
    
    def get_name(self) -> str:
        """获取声音名称"""
        return self.name
    
    def get_complete_blocks(self) -> List['BaseBlock']:
        """获取完成后执行的块"""
        return self.complete_blocks
    
    def validate(self) -> bool:
        """验证参数有效"""
        return bool(self.type_id and self.name)
