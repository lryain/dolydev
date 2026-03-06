"""
控制类动画块

包括延时、循环等控制逻辑。

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


class DelayBlock(BaseBlock):
    """延时块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlockType']]] = None):
        super().__init__('delay_ms', fields, statements)
        # 兼容 delay_ms 和 duration_ms
        self.delay_ms = self.get_field('delay_ms', self.get_field('duration_ms', 0))
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行延时"""
        if self.delay_ms > 0:
            logger.info(f"[DelayBlock] 延迟 {self.delay_ms}ms")
            await asyncio.sleep(self.delay_ms / 1000.0)
            logger.debug(f"[DelayBlock] 延迟完成")
    
    def get_duration(self) -> float:
        """返回延时时长（秒）"""
        return self.delay_ms / 1000.0
    
    def validate(self) -> bool:
        """验证延时值有效"""
        return self.delay_ms >= 0


class RepeatBlock(BaseBlock):
    """循环块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['BaseBlock']]] = None):
        super().__init__('repeat', fields, statements)
        self.times = self.get_field('times', 1)
        self.repeat_blocks = statements.get('repeat_statement', []) if statements else []
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行循环"""
        # 这个方法需要由执行器调用，因为需要递归执行子块
        # 在这里只是一个占位符
        logger.debug(f"Repeat block: {self.times} times, {len(self.repeat_blocks)} blocks")
    
    def validate(self) -> bool:
        """验证循环次数有效"""
        return self.times > 0
    
    def get_repeat_times(self) -> int:
        """获取循环次数"""
        return self.times
    
    def get_repeat_blocks(self) -> List['BaseBlock']:
        """获取循环体块列表"""
        return self.repeat_blocks
