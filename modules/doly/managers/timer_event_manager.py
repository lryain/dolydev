"""
定时器事件管理器

处理来自 widget_service 的定时器事件，播放相应的动画。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
from typing import Optional, Dict, Any
from pathlib import Path
import yaml

logger = logging.getLogger(__name__)


class TimerEventManager:
    """定时器事件管理器"""
    
    def __init__(self, config_path: str):
        """
        初始化定时器事件管理器
        
        Args:
            config_path: 配置文件路径
        """
        self.config_path = Path(config_path)
        self.config = {}
        self._animation_manager = None
        
        # 加载配置
        self._load_config()
        
        logger.info("TimerEventManager 初始化完成")
    
    def _load_config(self):
        """加载配置文件"""
        try:
            if not self.config_path.exists():
                logger.warning(f"配置文件不存在: {self.config_path}")
                self.config = {'timer_events': {}, 'voice_commands': {}}
                return
            
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config = yaml.safe_load(f) or {}
            
            timer_count = len(self.config.get('timer_events', {}))
            voice_count = len(self.config.get('voice_commands', {}))
            logger.info(f"加载定时器事件配置成功: {self.config_path}")
            logger.info(f"  定时器事件: {timer_count} 个")
            logger.info(f"  语音指令: {voice_count} 个")
            
        except Exception as e:
            logger.error(f"加载配置文件失败: {e}")
            self.config = {'timer_events': {}, 'voice_commands': {}}
    
    def set_animation_manager(self, animation_manager):
        """
        设置动画管理器
        
        Args:
            animation_manager: AnimationManager 实例
        """
        self._animation_manager = animation_manager
        logger.info("已设置动画管理器")
    
    def handle_timer_event(self, event_type: str, event_data: Optional[Dict[str, Any]] = None) -> bool:
        """
        处理定时器事件
        
        Args:
            event_type: 事件类型 (timer_start, timer_pause, countdown_complete等)
            event_data: 事件数据（可选）
            
        Returns:
            是否成功处理
        """
        try:
            # 获取事件配置
            timer_events = self.config.get('timer_events', {})
            event_config = timer_events.get(event_type)
            
            if not event_config:
                logger.debug(f"未找到定时器事件配置: {event_type}")
                return False
            
            # 检查是否启用
            if not event_config.get('enabled', True):
                logger.debug(f"定时器事件已禁用: {event_type}")
                return False
            
            # 获取动画和优先级
            animation = event_config.get('animation')
            priority = event_config.get('priority', 5)
            
            if not animation:
                logger.warning(f"定时器事件缺少动画配置: {event_type}")
                return False
            
            logger.info(f"🕐 [Timer] 定时器事件: {event_type} -> {animation}")
            
            # 播放动画
            if self._animation_manager:
                return self._animation_manager.play_animation(animation, priority=priority)
            else:
                logger.warning("[Timer] 动画管理器未设置")
                return False
                
        except Exception as e:
            logger.error(f"[Timer] 处理定时器事件失败: {e}", exc_info=True)
            return False
    
    def handle_voice_command(self, command: str) -> bool:
        """
        处理语音指令（如拍照）
        
        Args:
            command: 语音指令名称
            
        Returns:
            是否成功处理
        """
        try:
            # 获取语音指令配置
            voice_commands = self.config.get('voice_commands', {})
            cmd_config = voice_commands.get(command)
            
            if not cmd_config:
                logger.debug(f"未找到语音指令配置: {command}")
                return False
            
            # 检查是否启用
            if not cmd_config.get('enabled', True):
                logger.debug(f"语音指令已禁用: {command}")
                return False
            
            # 获取动画和优先级
            animation = cmd_config.get('animation')
            priority = cmd_config.get('priority', 5)
            
            if not animation:
                logger.warning(f"语音指令缺少动画配置: {command}")
                return False
            
            logger.info(f"📸 [Voice] 语音指令: {command} -> {animation}")
            
            # 播放动画
            if self._animation_manager:
                return self._animation_manager.play_animation(animation, priority=priority)
            else:
                logger.warning("[Voice] 动画管理器未设置")
                return False
                
        except Exception as e:
            logger.error(f"[Voice] 处理语音指令失败: {e}", exc_info=True)
            return False
    
    def reload_config(self):
        """重新加载配置"""
        logger.info("重新加载定时器事件配置...")
        self._load_config()
