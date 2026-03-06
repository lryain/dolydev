"""
事件节流器 - 实现事件冷却/去抖机制

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""
import time
import logging

logger = logging.getLogger(__name__)


class EventThrottler:
    """事件节流器，防止短时间内重复响应同一事件"""
    
    def __init__(self):
        self.cooldowns = {}  # event_name -> last_trigger_time
    
    def is_throttled(self, event_name: str, cooldown_time: float) -> bool:
        """
        检查事件是否在冷却中
        
        Args:
            event_name: 事件名称
            cooldown_time: 冷却时长（秒）
        
        Returns:
            True: 在冷却中，应该忽略
            False: 不在冷却中，可以响应
        """
        now = time.time()
        last_time = self.cooldowns.get(event_name, 0)
        
        if now - last_time < cooldown_time:
            logger.debug(
                f"[Throttler] 事件冷却中: {event_name} "
                f"(剩余 {cooldown_time - (now - last_time):.2f}s)"
            )
            return True
        
        self.cooldowns[event_name] = now
        logger.debug(f"[Throttler] 事件通过: {event_name}")
        return False
    
    def reset_cooldown(self, event_name: str = None):
        """
        重置冷却计时器
        
        Args:
            event_name: 指定事件（None表示重置所有）
        """
        if event_name:
            if event_name in self.cooldowns:
                del self.cooldowns[event_name]
                logger.debug(f"[Throttler] 重置冷却: {event_name}")
        else:
            self.cooldowns.clear()
            logger.debug("[Throttler] 重置所有冷却")
    
    def get_remaining_cooldown(self, event_name: str, cooldown_time: float) -> float:
        """
        获取剩余冷却时间
        
        Args:
            event_name: 事件名称
            cooldown_time: 冷却时长
        
        Returns:
            剩余冷却秒数（0表示无冷却）
        """
        now = time.time()
        last_time = self.cooldowns.get(event_name, 0)
        remaining = cooldown_time - (now - last_time)
        return max(0, remaining)
