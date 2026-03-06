"""
动画块基础类

定义所有动画块的抽象基类。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from abc import ABC, abstractmethod
from typing import Any, Dict, Optional, List, TYPE_CHECKING
import logging

from ..hardware_interfaces import HardwareInterfaces

if TYPE_CHECKING:
    from . import AnimationBlock

logger = logging.getLogger(__name__)


class BaseBlock(ABC):
    """动画块基类"""
    
    def __init__(self, block_type: str, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        """
        初始化动画块
        
        Args:
            block_type: 块类型
            fields: 字段字典
            statements: 语句字典，包含 complete_statement 等嵌套块
        """
        self.block_type = block_type
        self.fields = fields
        self.start_mode = fields.get('start', 1)  # 默认顺序执行
        # complete_statement: 当前块执行完成后要执行的嵌套块列表
        self.complete_blocks = statements.get('complete_statement', []) if statements else []
    
    @abstractmethod
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """
        执行动画块
        
        Args:
            interfaces: 硬件接口集合
        """
        pass
    
    def validate(self) -> bool:
        """
        验证参数有效性
        
        Returns:
            是否有效
        """
        return True
    
    def get_duration(self) -> Optional[float]:
        """
        获取执行时长（秒）
        
        Returns:
            执行时长，如果无法确定则返回 None
        """
        return None
    
    def is_parallel(self) -> bool:
        """
        是否并行执行
        
        Returns:
            True 表示并行，False 表示顺序
        """
        return self.start_mode == 0
    
    def get_field(self, name: str, default: Any = None) -> Any:
        """
        获取字段值
        
        Args:
            name: 字段名
            default: 默认值
            
        Returns:
            字段值
        """
        return self.fields.get(name, default)
    
    def get_complete_blocks(self) -> List['AnimationBlock']:
        """
        获取完成时要执行的嵌套块
        
        Returns:
            嵌套块列表
        """
        return self.complete_blocks
    
    def supports_complete_statement(self) -> bool:
        """
        是否支持 complete_statement 语句
        
        Returns:
            True 表示支持
        """
        return len(self.complete_blocks) > 0
    
    def __repr__(self) -> str:
        return f"{self.__class__.__name__}(type={self.block_type}, start_mode={self.start_mode})"


class StartAnimationBlock(BaseBlock):
    """动画开始块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('start_animation', fields, statements)
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """开始标记，不执行任何操作"""
        logger.debug("Animation started")
