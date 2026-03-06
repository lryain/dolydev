"""
小智情绪管理器

处理小智语音助手的情绪状态，并触发相应的动画表现

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import time
import logging
from typing import Optional, Dict, Any, Callable
from pathlib import Path

import yaml

logger = logging.getLogger(__name__)


class XiaozhiEmotionManager:
    """
    小智情绪管理器
    
    负责:
    - 订阅小智情绪状态
    - 情绪到动画的映射
    - 防抖处理
    - 触发动画和LED效果
    """
    
    def __init__(self, config_path: Optional[str] = None):
        """
        初始化情绪管理器
        
        Args:
            config_path: 配置文件路径
        """
        self.config_path = Path(config_path or "/home/pi/dolydev/config/xiaozhi_emotion_mapping.yaml")
        self.config: Dict[str, Any] = {}
        self.emotion_mapping: Dict[str, Dict] = {}
        
        # 状态
        self.last_emotion: str = "neutral"
        self.last_emotion_time_ms: int = 0
        self.enabled: bool = True
        
        # 外部依赖（由daemon设置）
        self.animation_manager = None
        self.led_controller = None
        self.state_provider: Optional[Callable[[], str]] = None
        
        # 加载配置
        self._load_config()
        
        logger.info("✅ [XiaozhiEmotionManager] 初始化完成")
    
    def _load_config(self) -> None:
        """加载情绪映射配置"""
        try:
            if self.config_path.exists():
                with open(self.config_path, 'r', encoding='utf-8') as f:
                    self.config = yaml.safe_load(f) or {}
                    
                self.emotion_mapping = self.config.get('emotion_mapping', {})
                settings = self.config.get('settings', {})
                
                self.min_interval_ms = settings.get('min_emotion_interval_ms', 500)
                self.priority = settings.get('emotion_animation_priority', 6)
                self.led_follows = settings.get('led_follows_emotion', True)
                self.enabled = settings.get('enabled', True)
                self.fallback_emotion = settings.get('fallback_emotion', 'neutral')
                
                self.ignore_states = set(self.config.get('ignore_in_states', []))
                
                logger.info(f"[XiaozhiEmotionManager] 加载配置完成: {len(self.emotion_mapping)} 种情绪映射")
            else:
                logger.warning(f"[XiaozhiEmotionManager] 配置文件不存在: {self.config_path}")
                self._use_default_config()
                
        except Exception as e:
            logger.error(f"[XiaozhiEmotionManager] 加载配置失败: {e}")
            self._use_default_config()
    
    def _use_default_config(self) -> None:
        """使用默认配置"""
        self.emotion_mapping = {
            'happy': {'animation': {'category': 'ANIMATION_HAPPY', 'level': 1}},
            'angry': {'animation': {'category': 'ANIMATION_ANGER', 'level': 1}},
            'sad': {'animation': {'category': 'ANIMATION_SAD', 'level': 1}},
            'neutral': {'animation': {'category': None, 'level': 1}},
        }
        self.min_interval_ms = 500
        self.priority = 6
        self.led_follows = True
        self.enabled = True
        self.fallback_emotion = 'neutral'
        self.ignore_states = set()
        logger.info("[XiaozhiEmotionManager] 使用默认配置")
    
    def reload_config(self) -> bool:
        """
        重新加载配置
        
        Returns:
            是否成功
        """
        try:
            self._load_config()
            logger.info("[XiaozhiEmotionManager] 配置重新加载完成")
            return True
        except Exception as e:
            logger.error(f"[XiaozhiEmotionManager] 重新加载配置失败: {e}")
            return False
    
    def set_animation_manager(self, manager) -> None:
        """设置动画管理器"""
        self.animation_manager = manager
        logger.debug("[XiaozhiEmotionManager] 已设置动画管理器")
    
    def set_led_controller(self, controller) -> None:
        """设置LED控制器"""
        self.led_controller = controller
        logger.debug("[XiaozhiEmotionManager] 已设置LED控制器")
    
    def set_state_provider(self, provider: Callable[[], str]) -> None:
        """设置状态提供器"""
        self.state_provider = provider
        logger.debug("[XiaozhiEmotionManager] 已设置状态提供器")
    
    def handle_emotion_event(self, event_data: Dict[str, Any]) -> bool:
        """
        处理情绪事件
        
        Args:
            event_data: 事件数据，包含 emotion 和 timestamp_ms
            
        Returns:
            是否成功处理
        """
        if not self.enabled:
            logger.debug("[XiaozhiEmotionManager] 情绪处理已禁用")
            return False
        
        emotion = event_data.get('emotion', '')
        timestamp_ms = event_data.get('timestamp_ms', int(time.time() * 1000))
        
        if not emotion:
            logger.warning("[XiaozhiEmotionManager] 收到空情绪事件")
            return False
        
        # 检查当前状态是否应该忽略情绪变化
        if self.state_provider:
            try:
                current_state = self.state_provider()
                if hasattr(current_state, 'value'):
                    current_state = current_state.value
                if current_state in self.ignore_states:
                    logger.debug(f"[XiaozhiEmotionManager] 当前状态 {current_state} 忽略情绪变化")
                    return False
            except Exception as e:
                logger.warning(f"[XiaozhiEmotionManager] 获取状态失败: {e}")
        
        # 防抖处理
        time_diff = timestamp_ms - self.last_emotion_time_ms
        if time_diff < self.min_interval_ms and emotion == self.last_emotion:
            logger.debug(f"[XiaozhiEmotionManager] 情绪变化过快，忽略: {emotion} (间隔 {time_diff}ms)")
            return False
        
        # 相同情绪不重复处理
        if emotion == self.last_emotion:
            logger.debug(f"[XiaozhiEmotionManager] 情绪未变化: {emotion}")
            return False
        
        logger.info(f"🎭 [XiaozhiEmotionManager] 情绪变化: {self.last_emotion} -> {emotion}")
        
        # 获取映射配置
        mapping = self.emotion_mapping.get(emotion)
        if not mapping:
            logger.warning(f"[XiaozhiEmotionManager] 未找到情绪映射: {emotion}，使用回退情绪")
            mapping = self.emotion_mapping.get(self.fallback_emotion, {})
        
        # 执行动画
        success = self._execute_emotion_response(emotion, mapping)
        
        # 更新状态
        self.last_emotion = emotion
        self.last_emotion_time_ms = timestamp_ms
        
        return success
    
    def _execute_emotion_response(self, emotion: str, mapping: Dict[str, Any]) -> bool:
        """
        执行情绪响应（动画和LED）
        
        Args:
            emotion: 情绪名称
            mapping: 映射配置
            
        Returns:
            是否成功执行
        """
        success = True
        
        # 播放动画
        animation_config = mapping.get('animation', {})
        category = animation_config.get('category')
        
        if category and self.animation_manager:
            level = animation_config.get('level', 1)
            try:
                # 使用动画分类播放
                result = self.animation_manager.play_category(
                    category=category,
                    level=level,
                    priority=self.priority
                )
                if result:
                    logger.info(f"🎬 [XiaozhiEmotionManager] 播放情绪动画: {category} level={level}")
                else:
                    logger.debug(f"[XiaozhiEmotionManager] 动画播放未执行: {category}")
            except AttributeError:
                # 尝试使用备用方法
                try:
                    result = self.animation_manager.play_animation(
                        animation=f"{category}.level{level}",
                        priority=self.priority
                    )
                    logger.info(f"🎬 [XiaozhiEmotionManager] 播放情绪动画(备用): {category}")
                except Exception as e:
                    logger.warning(f"[XiaozhiEmotionManager] 播放动画失败: {e}")
                    success = False
            except Exception as e:
                logger.error(f"[XiaozhiEmotionManager] 播放动画异常: {e}")
                success = False
        
        # 设置LED效果
        led_config = mapping.get('led', {})
        if led_config and self.led_follows and self.led_controller:
            try:
                effect = led_config.get('effect', 'breathe')
                color = led_config.get('color', '#FFFFFF')
                speed = led_config.get('speed', 50)
                side = led_config.get('side', 'both')
                
                self.led_controller.set_effect(
                    effect=effect,
                    color=color,
                    speed=speed,
                    side=side
                )
                logger.debug(f"💡 [XiaozhiEmotionManager] 设置LED: {effect} {color}")
            except Exception as e:
                logger.warning(f"[XiaozhiEmotionManager] 设置LED失败: {e}")
        
        return success
    
    def get_current_emotion(self) -> str:
        """获取当前情绪"""
        return self.last_emotion
    
    def get_emotion_info(self, emotion: str) -> Optional[Dict[str, Any]]:
        """
        获取情绪的详细信息
        
        Args:
            emotion: 情绪名称
            
        Returns:
            情绪配置信息
        """
        return self.emotion_mapping.get(emotion)
    
    def list_emotions(self) -> list:
        """列出所有支持的情绪"""
        return list(self.emotion_mapping.keys())
    
    def set_enabled(self, enabled: bool) -> None:
        """启用/禁用情绪处理"""
        self.enabled = enabled
        logger.info(f"[XiaozhiEmotionManager] 情绪处理: {'启用' if enabled else '禁用'}")
