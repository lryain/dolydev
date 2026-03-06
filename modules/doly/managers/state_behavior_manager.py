"""
状态行为管理器

管理不同状态下的行为配置和LED效果

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import yaml
import logging
from typing import Optional, Dict, Any, Callable
from pathlib import Path

logger = logging.getLogger(__name__)


class StateBehaviorManager:
    """状态行为管理器"""
    
    def __init__(self, config_path: str = "/home/pi/dolydev/config/state_behaviors.yaml"):
        """
        初始化状态行为管理器
        
        Args:
            config_path: 状态行为配置文件路径
        """
        self.config_path = Path(config_path)
        self.behaviors: Dict[str, Any] = {}
        self.sleep_schedules = []
        
        # LED 控制客户端(由外部设置)
        self.led_controller = None
        
        # 当前状态
        self.current_state: Optional[str] = None
        
        self._load_config()
        logger.info(f"StateBehaviorManager 初始化完成, 加载了 {len(self.behaviors)} 个状态配置")
    
    def _load_config(self):
        """加载配置文件"""
        try:
            if not self.config_path.exists():
                logger.warning(f"状态行为配置文件不存在: {self.config_path}")
                self.behaviors = {}
                return
            
            with open(self.config_path, 'r', encoding='utf-8') as f:
                data = yaml.safe_load(f) or {}
            
            # 提取状态配置
            self.behaviors = {k: v for k, v in data.items() if isinstance(v, dict)}
            
            # 解析睡眠时段
            self._parse_sleep_schedules()
            
            logger.info(f"加载状态行为配置成功: {self.config_path}")
            if self.sleep_schedules:
                logger.info(f"已加载睡眠时段: {self.sleep_schedules}")
        except Exception as e:
            logger.error(f"加载状态行为配置失败: {e}")
            self.behaviors = {}
    
    def _parse_sleep_schedules(self):
        """解析睡眠时段配置"""
        self.sleep_schedules = []
        try:
            sleep_cfg = self.behaviors.get('sleep')
            if sleep_cfg and isinstance(sleep_cfg, dict):
                for seg in sleep_cfg.get('schedule', []):
                    start = seg.get('start')
                    end = seg.get('end')
                    if start and end:
                        self.sleep_schedules.append((start, end))
        except Exception as e:
            logger.debug(f"解析睡眠时段失败: {e}")
    
    def set_led_controller(self, controller):
        """
        设置 LED 控制器
        
        Args:
            controller: LED 控制器实例
        """
        self.led_controller = controller
        logger.info("已设置 LED 控制器")
    
    def get_state_config(self, state: str) -> Optional[Dict[str, Any]]:
        """
        获取状态配置
        
        Args:
            state: 状态名称(如 idle, activated, exploring)
            
        Returns:
            状态配置字典
        """
        # 转换为小写
        state_key = state.lower()
        return self.behaviors.get(state_key)
    
    def is_state_enabled(self, state: str) -> bool:
        """
        检查状态是否启用
        
        Args:
            state: 状态名称
            
        Returns:
            是否启用
        """
        config = self.get_state_config(state)
        if not config:
            return False
        return config.get('enabled', False)
    
    def get_state_actions(self, state: str) -> list:
        """
        获取状态动作列表
        
        Args:
            state: 状态名称
            
        Returns:
            动作列表
        """
        config = self.get_state_config(state)
        if not config:
            return []
        
        actions = config.get('actions', [])
        # 过滤出启用的动作
        return [a for a in actions if a.get('enabled', True)]
    
    def get_state_intervals(self, state: str) -> tuple:
        """
        获取状态动作触发间隔
        
        Args:
            state: 状态名称
            
        Returns:
            (min_interval, max_interval) 元组
        """
        config = self.get_state_config(state)
        if not config:
            return (30, 60)  # 默认值
        
        min_interval = config.get('min_interval', 30)
        max_interval = config.get('max_interval', 60)
        return (min_interval, max_interval)
    
    def get_snooze_timeout(self, state: str) -> Optional[int]:
        """
        获取状态的 snooze 超时时间
        
        Args:
            state: 状态名称
            
        Returns:
            超时秒数，0或None表示禁用
        """
        config = self.get_state_config(state)
        if not config:
            return None
        
        snooze_after = config.get('snooze_after', 0)
        return snooze_after if snooze_after > 0 else None
    
    def get_nap_action(self, state: str) -> Optional[Dict[str, Any]]:
        """
        获取状态的 nap 动作配置
        
        Args:
            state: 状态名称
            
        Returns:
            动作配置字典
        """
        config = self.get_state_config(state)
        if not config:
            return None
        
        return config.get('nap_action')
    
    def apply_led_for_state(self, state: str) -> bool:
        """
        应用状态的 LED 配置
        
        Args:
            state: 状态名称
            
        Returns:
            是否成功
        """
        config = self.get_state_config(state)
        if not config:
            logger.debug(f"未找到状态配置: {state}")
            return False
        
        led_config = config.get('led')
        if not led_config:
            logger.debug(f"状态无 LED 配置: {state}")
            return False
        
        if not self.led_controller:
            logger.debug("LED 控制器未设置，使用回调函数控制 LED")
            return False
        
        try:
            effect = led_config.get('effect', 'solid')
            color = led_config.get('color', '#FFFFFF')
            side = led_config.get('side', 'both')
            speed = led_config.get('speed', 50)
            
            # 调用 LED 控制器
            # TODO: 实现具体的 LED 控制逻辑
            logger.info(f"[LED] 应用状态 LED: state={state}, effect={effect}, color={color}")
            
            # 示例调用(具体实现取决于LED控制器接口)
            # self.led_controller.set_effect(effect=effect, color=color, side=side, speed=speed)
            
            self.current_state = state
            return True
        except Exception as e:
            logger.error(f"应用 LED 配置失败: state={state}, error={e}")
            return False
    
    def get_tof_tracking_config(self) -> Optional[Dict[str, Any]]:
        """
        获取 TOF 跟踪配置
        
        Returns:
            TOF 跟踪配置字典
        """
        return self.behaviors.get('tof_tracking')
    
    def get_tof_exit_commands(self) -> list:
        """
        获取 TOF 跟踪退出指令列表
        
        Returns:
            退出指令列表
        """
        tof_config = self.get_tof_tracking_config()
        if not tof_config:
            return []
        
        return tof_config.get('exit_commands', [])
    
    def reload_config(self):
        """重新加载配置"""
        logger.info("重新加载状态行为配置...")
        self._load_config()
        
        # 如果有当前状态，重新应用LED
        if self.current_state:
            self.apply_led_for_state(self.current_state)
    
    def get_sleep_schedules(self) -> list:
        """
        获取睡眠时段配置
        
        Returns:
            [(start, end), ...] 列表
        """
        return self.sleep_schedules
