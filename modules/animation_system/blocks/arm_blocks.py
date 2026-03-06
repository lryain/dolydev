"""
手臂控制块

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


class ArmSetAngleBlock(BaseBlock):
    """手臂角度设置块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('arm_set_angle', fields, statements)
        self.angle = self.get_field('angle', 0)
        self.side = self.get_field('side', 0)
        self.speed = self.get_field('speed', 50)
        self.brake = self.get_field('brake', 0)
        # 舵机自动保持参数
        self.en_servo_autohold = self.get_field('en_servo_autohold', False)
        self.servo_autohold_duration = self.get_field('servo_autohold_duration', 3000)
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """设置手臂角度"""
        logger.info(f"[ArmSetAngleBlock.execute] 开始执行, angle={self.angle}, side={self.side}")
        
        if not interfaces.arm:
            logger.error("[ArmSetAngleBlock] Arm interface NOT available!")
            return
        
        side_str = {0: "全部", 1: "左臂", 2: "右臂"}.get(self.side, "未知")
        autohold_str = f", autohold={self.servo_autohold_duration}ms" if self.en_servo_autohold else ""
        logger.info(f"[ArmSetAngleBlock] 设置 {side_str} 手臂角度: {self.angle}°{autohold_str}")
        try:
            logger.info(f"[ArmSetAngleBlock] 调用 interfaces.arm.set_angle({self.angle}, {self.side}, "
                       f"speed={self.speed}, brake={self.brake}, "
                       f"en_servo_autohold={self.en_servo_autohold}, "
                       f"servo_autohold_duration={self.servo_autohold_duration})")
            await interfaces.arm.set_angle(
                self.angle, 
                self.side,
                speed=self.speed,
                brake=self.brake,
                en_servo_autohold=self.en_servo_autohold,
                servo_autohold_duration=self.servo_autohold_duration
            )
            logger.info(f"[ArmSetAngleBlock] 手臂角度设置完成")
        except Exception as e:
            logger.error(f"[ArmSetAngleBlock] 设置手臂角度失败: {e}", exc_info=True)
            raise
    
    def validate(self) -> bool:
        """验证参数有效"""
        return 0 <= self.angle <= 180 and self.side in [0, 1, 2]
