"""
Doly 管理器模块

提供各种功能管理器:
- WidgetManager: Widget 显示管理
- VoiceCommandManager: 语音指令处理
- SensorEventManager: 传感器事件处理
- AnimationManager: 动画播放管理
- StateBehaviorManager: 状态行为管理
- TimerEventManager: 定时器事件管理
- XiaozhiEmotionManager: 小智情绪处理
- FaceRecoManager: 人脸识别事件处理

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from .widget_manager import WidgetManager
from .voice_command_manager import VoiceCommandManager, VoiceCommandHandlers
from .sensor_event_manager import SensorEventManager
from .animation_manager import AnimationManager
from .state_behavior_manager import StateBehaviorManager
from .timer_event_manager import TimerEventManager
from .xiaozhi_emotion_manager import XiaozhiEmotionManager
from .face_reco_manager import FaceRecoManager

__all__ = [
    'WidgetManager', 
    'VoiceCommandManager', 
    'VoiceCommandHandlers',
    'SensorEventManager',
    'AnimationManager',
    'StateBehaviorManager',
    'TimerEventManager',
    'XiaozhiEmotionManager',
    'FaceRecoManager'
]
