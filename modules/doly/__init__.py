"""
Doly Daemon 模块

主程序模块，负责统一控制 Doly 机器人。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from .daemon import DolyDaemon
from .state_machine import DolyState, DolyStateMachine
from .event_bus import EventBus, DolyEvent, EventType
from .command_mapper import CommandMapper

__all__ = [
    'DolyDaemon',
    'DolyState',
    'DolyStateMachine',
    'EventBus',
    'DolyEvent',
    'EventType',
    'CommandMapper',
]

__version__ = '0.1.0'
