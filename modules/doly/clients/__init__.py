"""
Doly Clients - ZMQ 客户端模块

提供与各个服务的 ZMQ 通信客户端

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from .eye_engine_client import EyeEngineClient
from .widget_service_client import WidgetServiceClient

__all__ = [
    'EyeEngineClient',
    'WidgetServiceClient',
]
