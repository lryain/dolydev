"""
小智客户端管理器

处理来自小智云端 AI 的情绪、动作和意图指令

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
from typing import Optional, Dict, Any, Callable
from modules.doly.event_bus import EventBus, EventType, DolyEvent

logger = logging.getLogger(__name__)


class XiaozhiManager:
    """
    小智管理器
    
    职责：
    1. 处理来自小智的情绪变化
    2. 处理来自小智的动作指令
    3. 处理来自小智的意图指令
    """
    
    def __init__(self, event_bus: EventBus):
        self.event_bus = event_bus
        self._emotion_callback: Optional[Callable[[str, int], None]] = None
        self._action_callback: Optional[Callable[[str, Dict[str, Any], int], None]] = None
        self._intent_callback: Optional[Callable[[str, Dict[str, Any], str], None]] = None
        
        logger.info("[XiaozhiManager] 初始化完成")
    
    # ==================== 回调注册 ====================
    
    def register_emotion_callback(self, callback: Callable[[str, int], None]) -> None:
        """
        注册情绪处理回调
        
        Args:
            callback: (emotion: str, intensity: int) -> None
        """
        self._emotion_callback = callback
        logger.info("[XiaozhiManager] 已注册情绪回调")
    
    def register_action_callback(self, callback: Callable[[str, Dict[str, Any], int], None]) -> None:
        """
        注册动作处理回调
        
        Args:
            callback: (action_type: str, params: dict, priority: int) -> None
        """
        self._action_callback = callback
        logger.info("[XiaozhiManager] 已注册动作回调")
    
    def register_intent_callback(self, callback: Callable[[str, Dict[str, Any], str], None]) -> None:
        """
        注册意图处理回调
        
        Args:
            callback: (intent: str, entities: dict, text: str) -> None
        """
        self._intent_callback = callback
        logger.info("[XiaozhiManager] 已注册意图回调")
    
    # ==================== 事件处理 ====================
    
    def handle_emotion(self, event: DolyEvent) -> None:
        """
        处理小智情绪变化
        
        事件格式：
        {
            "emotion": "happy",
            "source": "xiaozhi",
            "intensity": 5
        }
        """
        try:
            emotion = event.data.get('emotion', 'neutral')
            intensity = event.data.get('intensity', 5)
            source = event.data.get('source', 'xiaozhi')
            
            logger.info(f"[XiaozhiManager] 收到情绪: {emotion} (source={source}, intensity={intensity})")
            
            if self._emotion_callback:
                self._emotion_callback(emotion, intensity)
            else:
                logger.warning("[XiaozhiManager] 未注册情绪回调，忽略")
                
        except Exception as e:
            logger.error(f"[XiaozhiManager] 处理情绪事件失败: {e}", exc_info=True)
    
    def handle_action(self, event: DolyEvent) -> None:
        """
        处理小智动作指令
        
        事件格式：
        {
            "action": "play_animation",
            "params": {...},
            "priority": 5
        }
        """
        try:
            action_type = event.data.get('action', '')
            params = event.data.get('params', {})
            priority = event.data.get('priority', 5)
            
            logger.info(f"[XiaozhiManager] 收到动作: {action_type} (priority={priority})")
            logger.debug(f"[XiaozhiManager] 动作参数: {params}")
            
            if self._action_callback:
                self._action_callback(action_type, params, priority)
            else:
                logger.warning("[XiaozhiManager] 未注册动作回调，忽略")
                
        except Exception as e:
            logger.error(f"[XiaozhiManager] 处理动作事件失败: {e}", exc_info=True)
    
    def handle_intent(self, event: DolyEvent) -> None:
        """
        处理小智意图指令
        
        事件格式：
        {
            "intent": "greeting",
            "entities": {...},
            "text": "你好啊"  // 可选
        }
        """
        try:
            intent = event.data.get('intent', '')
            entities = event.data.get('entities', {})
            text = event.data.get('text', '')
            
            logger.info(f"[XiaozhiManager] 收到意图: {intent}")
            logger.debug(f"[XiaozhiManager] 意图实体: {entities}")
            
            if self._intent_callback:
                self._intent_callback(intent, entities, text)
            else:
                logger.warning("[XiaozhiManager] 未注册意图回调，忽略")
                
        except Exception as e:
            logger.error(f"[XiaozhiManager] 处理意图事件失败: {e}", exc_info=True)
