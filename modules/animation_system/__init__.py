"""
Doly Animation System

一个用于解析和执行 Doly 机器人动画 XML 文件的 Python 模块。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

__version__ = '1.0.0'
__author__ = 'Doly Animation System'

from .animation_manager import AnimationManager
from .executor import AnimationExecutor
from .hardware_interfaces import HardwareInterfaces
from .parser import AnimationParser

__all__ = [
    'AnimationManager',
    'AnimationExecutor',
    'HardwareInterfaces',
    'AnimationParser',
]
