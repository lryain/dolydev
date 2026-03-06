"""
Doly 眼睛动画引擎

用于控制 Doly 机器人的 LCD 眼睛显示

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

__version__ = "1.1.0"
__author__ = "Doly Project"

from .config import EyeState, EngineConfig, LcdSide, LidType
from .engine import EyeEngine
from .controller import EyeController
from .renderer import EyeRenderer
from .config_loader import (
    EyeConfigLoader, get_config_loader, reload_config,
    IrisConfig, BackgroundConfig, LidImageConfig, LidAnimationConfig,
    EyeAnimation, EyeAnimationEvent
)
from .eye_animation import (
    EyeAnimationPlayer,
    list_animations, list_categories, get_animations_in_category
)

__all__ = [
    # 核心类
    "EyeEngine",
    "EyeController", 
    "EyeRenderer",
    
    # 配置
    "EyeState",
    "EngineConfig",
    "LcdSide",
    "LidType",
    
    # 配置加载器
    "EyeConfigLoader",
    "get_config_loader",
    "reload_config",
    "IrisConfig",
    "BackgroundConfig",
    "LidImageConfig",
    "LidAnimationConfig",
    "EyeAnimation",
    "EyeAnimationEvent",
    
    # 动画播放
    "EyeAnimationPlayer",
    "list_animations",
    "list_categories",
    "get_animations_in_category",
]
