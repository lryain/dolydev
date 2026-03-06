"""
硬件接口抽象层 (向后兼容性导出)

所有抽象基类现在定义在 hardware_interfaces.py 中，此文件保留用于向后兼容。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from .hardware_interfaces import (
    EyeInterface,
    LEDInterface,
    SoundInterface,
    ArmInterface,
    DriveInterface,
    HardwareInterfaces
)

__all__ = [
    'EyeInterface',
    'LEDInterface',
    'SoundInterface',
    'ArmInterface',
    'DriveInterface',
    'HardwareInterfaces'
]
