"""
LCD 驱动接口协议

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from typing import Protocol, runtime_checkable
from ..config import LcdSide


@runtime_checkable
class ILcdDriver(Protocol):
    """
    LCD 驱动接口协议
    
    定义与 LCD 硬件交互的标准接口。
    所有 LCD 驱动实现都必须遵循此协议。
    """
    
    def init(self) -> bool:
        """
        初始化 LCD 硬件
        
        Returns:
            成功返回 True，失败返回 False
        """
        ...
    
    def release(self) -> None:
        """释放资源"""
        ...
    
    def write(self, data: bytes) -> bool:
        """
        写入帧数据到 LCD
        
        Args:
            data: RGB888 格式像素数据 (240*240*3 = 172800 bytes)
        
        Returns:
            成功返回 True
        """
        ...
    
    def write_frame(self, data: bytes) -> bool:
        """等同于 write"""
        ...
    
    def write_frame_rgba(self, side: LcdSide, data: bytes) -> bool:
        """
        写入 RGBA 帧数据到 LCD (自动转换)
        
        Args:
            side: 目标 LCD (LEFT/RIGHT)
            data: RGBA8888 格式像素数据 (240*240*4 = 230400 bytes)
        
        Returns:
            成功返回 True
        """
        ...
    
    def set_brightness(self, side: LcdSide, level: int) -> None:
        """
        设置亮度
        
        Args:
            side: 目标 LCD (LEFT/RIGHT/BOTH)
            level: 亮度级别 (0-100)
        """
        ...
    
    def fill_color(self, side: LcdSide, r: int, g: int, b: int) -> None:
        """
        纯色填充
        
        Args:
            side: 目标 LCD (LEFT/RIGHT/BOTH)
            r, g, b: RGB 颜色值 (0-255)
        """
        ...
    
    def is_active(self) -> bool:
        """
        检查硬件是否就绪
        
        Returns:
            就绪返回 True
        """
        ...
