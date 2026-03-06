"""
传感器事件管理器

处理传感器事件的识别、过滤和分发

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
from typing import Optional, Dict, Any, Callable
from modules.doly.gesture_recognizer import GestureRecognizer
from modules.doly.event_throttler import EventThrottler
from modules.doly.sensor_config import SensorEventConfig

logger = logging.getLogger(__name__)


class SensorEventManager:
    """传感器事件管理器"""
    
    def __init__(self, config_path: str = "/home/pi/dolydev/config/sensor_event_mapping.yaml"):
        """
        初始化传感器事件管理器
        
        Args:
            config_path: 传感器事件映射配置文件路径
        """
        # 核心组件
        self.gesture_recognizer = GestureRecognizer(history_window=2.0)
        self.event_throttler = EventThrottler()
        self.sensor_config = SensorEventConfig(config_path)
        
        # 事件处理器
        self.handlers: Dict[str, Callable] = {}
        
        # 状态获取器(用于状态依赖的事件处理)
        self.get_current_state: Optional[Callable] = None
        
        logger.info("SensorEventManager 初始化完成")
    
    def set_state_provider(self, state_provider: Callable):
        """
        设置状态提供器
        
        Args:
            state_provider: 返回当前状态的函数
        """
        self.get_current_state = state_provider
        logger.info("已设置状态提供器")
    
    def register_handler(self, event_type: str, handler: Callable):
        """
        注册事件处理器
        
        Args:
            event_type: 事件类型(touch, cliff, tof, etc.)
            handler: 处理函数
        """
        self.handlers[event_type] = handler
        logger.info(f"注册传感器事件处理器: {event_type}")
    
    def handle_sensor_event(self, event: Dict[str, Any]) -> bool:
        """
        处理传感器事件
        
        Args:
            event: 传感器事件字典
            
        Returns:
            是否成功处理
        """
        topic = event.get('topic', '')
        
        # 分发到相应的处理器
        if 'touch' in topic:
            return self._handle_touch_event(event)
        elif 'cliff' in topic:
            return self._handle_cliff_event(event)
        elif 'tof' in topic:
            return self._handle_tof_event(event)
        elif 'imu' in topic:
            return self._handle_imu_event(event)
        else:
            logger.debug(f"未处理的传感器事件: {topic}")
            return False
    
    def _handle_touch_event(self, event: Dict[str, Any]) -> bool:
        """
        处理触摸事件
        
        Args:
            event: 触摸事件
            
        Returns:
            是否成功处理
        """
        topic = event.get('topic', '')
        pin = event.get('pin', '')
        
        # 手势事件(drive_service已识别的高级手势)
        if 'gesture' in topic:
            gesture = event.get('gesture', '')
            side = event.get('side', '')  # ✅ 使用 side 字段而不是 pin
            logger.info(f"👆 [Touch] 触摸手势: side={side}, gesture={gesture}")
            
            # 通过手势识别器进一步处理
            enhanced_gesture = self.gesture_recognizer.process_gesture(pin, gesture)
            if enhanced_gesture != gesture:
                logger.info(f"[Gesture] 识别增强手势: {gesture} -> {enhanced_gesture}")
            
            # 构建事件键（使用 side 而不是 pin，确保与配置文件匹配）
            # 配置格式: touch_left_single_tap, touch_right_double_tap, 等
            event_key = f"touch_{side}_{enhanced_gesture.lower()}"
            
            # 冷却检查
            if not self.event_throttler.should_process(event_key):
                logger.debug(f"[Throttler] 事件冷却中，跳过: {event_key}")
                return False
            
            # 获取当前状态
            current_state = None
            if self.get_current_state:
                try:
                    state_obj = self.get_current_state()
                    current_state = state_obj.value if hasattr(state_obj, 'value') else str(state_obj)
                except Exception as e:
                    logger.warning(f"获取当前状态失败: {e}")
            
            # 获取动作配置
            action_config = self.sensor_config.get_action(event_key, current_state)
            if not action_config:
                logger.debug(f"未找到动作配置: {event_key}, state={current_state}")
                return False
            
            # 执行动作
            logger.info(f"[Touch] 执行动作: {event_key} -> {action_config}")
            return self._execute_action(event_key, action_config)
        
        # 原始触摸事件
        else:
            state = event.get('state', 0)
            logger.debug(f"[Touch] 原始事件: pin={pin}, state={state}")
            # 原始事件不处理，等待drive_service的手势识别
            return False
    
    def _handle_cliff_event(self, event: Dict[str, Any]) -> bool:
        """
        处理悬崖事件(高优先级，立即响应)
        
        Args:
            event: 悬崖事件
            
        Returns:
            是否成功处理
        """
        topic = event.get('topic', '')
        
        # 悬崖模式事件(drive_service已识别的高级模式)
        if 'pattern' in topic:
            pattern = event.get('pattern', '')
            positions = event.get('positions', [])
            logger.warning(f"⚠️ [Cliff] 检测到悬崖模式: {pattern}, positions={positions}")
            
            # 构建事件键
            event_key = f"cliff_{pattern}"
            
            # 悬崖事件不受冷却限制！
            
            # 获取动作配置
            action_config = self.sensor_config.get_action(event_key)
            if not action_config:
                logger.warning(f"未找到悬崖动作配置: {event_key}")
                return False
            
            # 执行动作
            logger.info(f"[Cliff] 执行避让: {event_key} -> {action_config}")
            return self._execute_action(event_key, action_config, priority='CRITICAL')
        
        return False
    
    def _handle_tof_event(self, event: Dict[str, Any]) -> bool:
        """
        处理TOF事件
        
        Args:
            event: TOF事件
            
        Returns:
            是否成功处理
        """
        # TOF事件由TOF集成模块处理
        logger.debug(f"[TOF] 事件: {event.get('topic')}")
        
        # 调用注册的TOF处理器(如果有)
        handler = self.handlers.get('tof')
        if handler:
            return handler(event)
        
        return False
    
    def _handle_imu_event(self, event: Dict[str, Any]) -> bool:
        """
        处理IMU事件
        
        Args:
            event: IMU事件
            
        Returns:
            是否成功处理
        """
        # IMU事件处理(如倾斜、震动等)
        logger.debug(f"[IMU] 事件: {event.get('topic')}")
        
        # 调用注册的IMU处理器(如果有)
        handler = self.handlers.get('imu')
        if handler:
            return handler(event)
        
        return False
    
    def _execute_action(self, event_key: str, action_config: Dict[str, Any], 
                       priority: str = 'NORMAL') -> bool:
        """
        执行动作
        
        Args:
            event_key: 事件键
            action_config: 动作配置
            priority: 优先级
            
        Returns:
            是否成功执行
        """
        action_type = action_config.get('type')
        
        # 查找对应的处理器
        handler = self.handlers.get('action')
        if not handler:
            logger.warning(f"未注册动作处理器，无法执行: {event_key}")
            return False
        
        try:
            # 调用动作处理器
            result = handler(
                action_type=action_type,
                action_config=action_config,
                event_key=event_key,
                priority=priority
            )
            
            # 记录冷却
            cooldown = action_config.get('cooldown', 1.0)
            self.event_throttler.record_event(event_key, cooldown)
            
            return result
        except Exception as e:
            logger.error(f"执行动作异常: {event_key}, error={e}", exc_info=True)
            return False
    
    def reset_gesture_history(self):
        """重置手势历史(例如状态切换时)"""
        self.gesture_recognizer.reset_history()
        logger.debug("[Gesture] 手势历史已重置")
    
    def reload_config(self):
        """重新加载配置"""
        try:
            self.sensor_config = SensorEventConfig(self.sensor_config.config_path)
            logger.info("传感器事件配置已重新加载")
        except Exception as e:
            logger.error(f"重新加载传感器配置失败: {e}")
