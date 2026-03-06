"""
Widget 管理器

协调 widget_service，处理 widget 显示逻辑
注意：widget_service 会自己管理 LCD，eyeEngine 会自动监听 lcd_request/released 事件

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
from typing import Optional, Dict, Any
from modules.doly.clients import WidgetServiceClient

logger = logging.getLogger(__name__)


class WidgetManager:
    """Widget 显示管理器（仅负责发送命令到 widget_service）"""
    
    def __init__(self, widget_client: Optional[WidgetServiceClient] = None):
        """
        初始化 Widget 管理器
        
        Args:
            widget_client: WidgetServiceClient 实例(可选)
        """
        self.widget_client = widget_client or WidgetServiceClient()
        logger.info("WidgetManager 初始化完成")
    
    # ===== Clock & Date =====
    
    def show_clock(self, timeout: Optional[int] = None, tts: Optional[str] = None) -> bool:
        """
        显示时钟 widget (widget_service 会自动管理 LCD 互斥)
        
        Args:
            timeout: 超时时间(秒)
            tts: TTS 文本或 True (可选，为 True 时使用 announce_time)
            
        Returns:
            是否成功
        """
        # 如果 tts=True，使用 announce_time 进行语音报时
        if tts is True:
            logger.info("执行语音报时 (announce_time)")
            self.widget_client.announce_time(language="zh")
        elif isinstance(tts, str) and tts:
            logger.info(f"TODO: 播放 TTS: {tts}")
        
        # 直接发送命令到 widget_service，它会自动管理 LCD
        return self.widget_client.show_clock(timeout=timeout)
    
    def show_date(self, timeout: Optional[int] = None, tts: Optional[str] = None) -> bool:
        """
        显示日期 widget (widget_service 会自动管理 LCD 互斥)
        
        Args:
            timeout: 超时时间(秒)
            tts: TTS 文本或 True (可选，为 True 时调用语音播报)
            
        Returns:
            是否成功
        """
        if tts is True:
            logger.info("执行语音报时 (announce_time)")
            self.widget_client.announce_time(language="zh")
        elif isinstance(tts, str) and tts:
            logger.info(f"TODO: 播放 TTS: {tts}")
        
        # 直接发送命令到 widget_service
        return self.widget_client.show_date(timeout=timeout)
    
    # ===== Timer =====
    
    def start_countdown(self, duration: int, auto_start: bool = False,
                       timeout: Optional[int] = None, tts: Optional[str] = None) -> bool:
        """
        启动倒计时 widget (widget_service 会自动管理 LCD 互斥)
        
        Args:
            duration: 倒计时时长(秒)
            auto_start: 是否自动开始
            timeout: 超时时间(秒)
            tts: TTS 文本(可选)
            
        Returns:
            是否成功
        """
        if tts:
            logger.info(f"TODO: 播放 TTS: {tts}")
        
        # 直接发送命令到 widget_service
        return self.widget_client.start_countdown(duration=duration, auto_start=auto_start, timeout=timeout)
    
    def start_timer(self, auto_start: bool = False, timeout: Optional[int] = None,
                   tts: Optional[str] = None) -> bool:
        """
        启动正计时 widget (widget_service 会自动管理 LCD 互斥)
        
        Args:
            auto_start: 是否自动开始
            timeout: 超时时间(秒)
            tts: TTS 文本(可选)
            
        Returns:
            是否成功
        """
        if tts:
            logger.info(f"TODO: 播放 TTS: {tts}")
        
        # 直接发送命令到 widget_service
        return self.widget_client.start_timer(auto_start=auto_start, timeout=timeout)
    
    def timer_control(self, action: str, tts: Optional[str] = None) -> bool:
        """
        控制计时器
        
        Args:
            action: 控制动作(start/pause/resume/cancel)
            tts: TTS 文本(可选)
            
        Returns:
            是否成功
        """
        if tts:
            logger.info(f"TODO: 播放 TTS: {tts}")
        
        # 发送控制命令到 widget_service
        return self.widget_client.timer_control(control_action=action)
    
    # ===== Alarm =====
    
    def set_alarm(self, hour: int, minute: int, timeout: Optional[int] = None,
                 tts: Optional[str] = None) -> bool:
        """
        设置闹钟 widget (widget_service 会自动管理 LCD 互斥)
        
        Args:
            hour: 小时(0-23)
            minute: 分钟(0-59)
            timeout: 超时时间(秒)
            tts: TTS 文本(可选)
            
        Returns:
            是否成功
        """
        if tts:
            logger.info(f"TODO: 播放 TTS: {tts}")
        
        # 直接发送命令到 widget_service
        return self.widget_client.set_alarm(hour=hour, minute=minute, timeout=timeout)
    
    # ===== Weather =====
    
    def show_weather(self, location: Optional[str] = None, timeout: Optional[int] = None,
                    tts: Optional[str] = None) -> bool:
        """
        显示天气 widget (widget_service 会自动管理 LCD 互斥)
        
        Args:
            location: 位置(可选)
            timeout: 超时时间(秒)
            tts: TTS 文本(可选)
            
        Returns:
            是否成功
        """
        if tts:
            logger.info(f"TODO: 播放 TTS: {tts}")
        
        # 直接发送命令到 widget_service
        return self.widget_client.show_weather(location=location, timeout=timeout)
    
    # ===== Clock API (查询时间/报时) =====
    
    def get_time(self) -> Optional[Dict[str, Any]]:
        """
        查询当前时间 (通过 widget_service 的 REQ/REP API)
        
        Returns:
            时间字典，包含 hour/minute/second 等
        """
        return self.widget_client.get_time()
    
    def chime_now(self, language: str = "zh") -> bool:
        """
        触发整点报时 (通过 widget_service 的 REQ/REP API)
        
        Args:
            language: 语言代码 (zh/en)
            
        Returns:
            是否成功
        """
        return self.widget_client.chime_now(language=language)
    
    def announce_time(self, language: str = "zh") -> bool:
        """
        语音报时 (通过 widget_service 的 REQ/REP API)
        
        Args:
            language: 语言代码 (zh/en)
            
        Returns:
            是否成功
        """
        return self.widget_client.announce_time(language=language)
    
    # ===== Status =====
    
    def get_status(self) -> Optional[Dict[str, Any]]:
        """获取 widget_service 状态"""
        return self.widget_client.get_status()
    
    # ===== Cleanup =====
    
    def close(self):
        """关闭管理器"""
        self.widget_client.close()
        logger.info("WidgetManager 已关闭")
