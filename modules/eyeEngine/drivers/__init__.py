"""
驱动模块

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from .interfaces import ILcdDriver

# LCD 驱动
try:
    from .lcd_driver import LcdDriver
except ImportError:
    LcdDriver = None

__all__ = ["ILcdDriver", "LcdDriver"]
