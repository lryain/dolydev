"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

#!/usr/bin/env python3
"""
Eye 显示/隐藏管理器

负责协调 eyeEngine 和 widgets 之间的互斥显示逻辑
包括超时自动恢复、事件触发恢复等机制
"""

import time
import logging
import threading
from typing import Optional, Set, Callable
from pathlib import Path
import yaml

logger = logging.getLogger(__name__)


class EyeVisibilityManager:
    """Eye 显示/隐藏管理器"""
    
    def __init__(self, config_path: str = None, zmq_publisher: Callable = None):
        """
        Args:
            config_path: 配置文件路径
            zmq_publisher: ZMQ 事件发布函数 (topic, data)
        """
        self.config = self._load_config(config_path)
        self.zmq_publisher = zmq_publisher
        
        # 状态管理
        self.eye_visible = True  # eye 是否可见
        self.widget_visible = False  # widget 是否可见
        self.current_widget = None  # 当前显示的 widget 名称
        
        # 定时器
        self.restore_timer: Optional[threading.Timer] = None
        self.restore_lock = threading.Lock()
        
        # 强制恢复事件集合
        self.force_restore_events: Set[str] = set(
            self.config.get('force_restore_events', [])
        )
        
        logger.info(f"EyeVisibilityManager 初始化完成，自动恢复: {self.is_auto_restore_enabled()}")
    
    def _load_config(self, config_path: str = None) -> dict:
        """加载配置文件"""
        if config_path is None:
            config_path = Path(__file__).parent.parent.parent / 'config' / 'eye_visibility.yaml'
        
        try:
            with open(config_path, 'r', encoding='utf-8') as f:
                config = yaml.safe_load(f)
                logger.info(f"加载配置文件: {config_path}")
                return config or {}
        except Exception as e:
            logger.warning(f"无法加载配置文件 {config_path}: {e}，使用默认配置")
            return {
                'auto_restore': {'enabled': True, 'timeout': 30},
                'force_restore_events': [],
                'pausable_widgets': [],
                'manual_mode': False
            }
    
    def is_auto_restore_enabled(self) -> bool:
        """是否启用自动恢复"""
        auto_restore = self.config.get('auto_restore', {})
        return auto_restore.get('enabled', True) and auto_restore.get('timeout', 0) > 0
    
    def get_restore_timeout(self) -> int:
        """获取自动恢复超时时间（秒）"""
        return self.config.get('auto_restore', {}).get('timeout', 30)
    
    def is_manual_mode(self) -> bool:
        """是否手动控制模式"""
        return self.config.get('manual_mode', False)
    
    def show_widget(self, widget_name: str, timeout: Optional[int] = None):
        """
        显示 widget，隐藏 eye
        
        Args:
            widget_name: widget 名称 (clock, timer, countdown, etc.)
            timeout: 自定义超时时间（秒），None=使用配置的默认值，0=禁用超时
        """
        with self.restore_lock:
            logger.info(f"显示 widget: {widget_name}, timeout={timeout}")
            
            # 取消现有的定时器
            if self.restore_timer:
                self.restore_timer.cancel()
                self.restore_timer = None
            
            # 更新状态
            self.eye_visible = False
            self.widget_visible = True
            self.current_widget = widget_name
            
            # 发布事件
            self._publish_event('eye.visibility.hidden', {'widget': widget_name})
            
            # 设置自动恢复定时器
            if not self.is_manual_mode():
                effective_timeout = timeout if timeout is not None else self.get_restore_timeout()
                
                if effective_timeout > 0:
                    logger.info(f"启动自动恢复定时器: {effective_timeout} 秒")
                    self.restore_timer = threading.Timer(effective_timeout, self._auto_restore)
                    self.restore_timer.daemon = True
                    self.restore_timer.start()
                else:
                    logger.info("超时为0，不启动自动恢复定时器")
    
    def restore_eye(self, reason: str = "manual"):
        """
        恢复 eye 显示，隐藏 widget
        
        Args:
            reason: 恢复原因 (manual, timeout, force_event)
        """
        with self.restore_lock:
            if self.eye_visible:
                logger.debug("Eye 已经可见，无需恢复")
                return
            
            logger.info(f"恢复 eye 显示，原因: {reason}, 之前的 widget: {self.current_widget}")
            
            # 取消定时器
            if self.restore_timer:
                self.restore_timer.cancel()
                self.restore_timer = None
            
            # 更新状态
            previous_widget = self.current_widget
            self.eye_visible = True
            self.widget_visible = False
            self.current_widget = None
            
            # 发布事件
            self._publish_event('eye.visibility.restored', {
                'reason': reason,
                'previous_widget': previous_widget
            })
    
    def _auto_restore(self):
        """自动恢复回调（定时器触发）"""
        logger.info("自动恢复定时器触发")
        self.restore_eye(reason="timeout")
    
    def handle_event(self, event_name: str) -> bool:
        """
        处理事件，判断是否需要强制恢复 eye
        
        Args:
            event_name: 事件名称
            
        Returns:
            是否触发了强制恢复
        """
        if event_name in self.force_restore_events:
            if not self.eye_visible:
                logger.info(f"事件 {event_name} 触发强制恢复 eye")
                self.restore_eye(reason="force_event")
                return True
        return False
    
    def pause_auto_restore(self):
        """暂停自动恢复（取消定时器，但保持状态）"""
        with self.restore_lock:
            if self.restore_timer:
                logger.info("暂停自动恢复定时器")
                self.restore_timer.cancel()
                self.restore_timer = None
    
    def resume_auto_restore(self, timeout: Optional[int] = None):
        """恢复自动恢复（重新启动定时器）"""
        with self.restore_lock:
            if not self.eye_visible and not self.is_manual_mode():
                effective_timeout = timeout if timeout is not None else self.get_restore_timeout()
                if effective_timeout > 0:
                    logger.info(f"恢复自动恢复定时器: {effective_timeout} 秒")
                    if self.restore_timer:
                        self.restore_timer.cancel()
                    self.restore_timer = threading.Timer(effective_timeout, self._auto_restore)
                    self.restore_timer.daemon = True
                    self.restore_timer.start()
    
    def set_manual_mode(self, enabled: bool):
        """设置手动控制模式"""
        logger.info(f"设置手动模式: {enabled}")
        self.config['manual_mode'] = enabled
        
        if enabled:
            # 进入手动模式，取消自动恢复定时器
            self.pause_auto_restore()
    
    def get_status(self) -> dict:
        """获取当前状态"""
        return {
            'eye_visible': self.eye_visible,
            'widget_visible': self.widget_visible,
            'current_widget': self.current_widget,
            'auto_restore_enabled': self.is_auto_restore_enabled(),
            'manual_mode': self.is_manual_mode(),
            'has_restore_timer': self.restore_timer is not None
        }
    
    def _publish_event(self, topic: str, data: dict):
        """发布 ZMQ 事件"""
        if self.zmq_publisher:
            try:
                self.zmq_publisher(topic, data)
            except Exception as e:
                logger.error(f"发布事件失败 {topic}: {e}")
