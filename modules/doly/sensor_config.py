"""
传感器事件配置加载器

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""
import yaml
import logging
from pathlib import Path
from typing import Dict, Any, Optional

logger = logging.getLogger(__name__)


class SensorEventConfig:
    """传感器事件配置管理器"""
    
    def __init__(self, config_path: str = None):
        """
        Args:
            config_path: 配置文件路径（默认使用项目根目录下的config/sensor_event_mapping.yaml）
        """
        if config_path is None:
            # 默认路径
            project_root = Path(__file__).parent.parent.parent
            config_path = project_root / "config" / "sensor_event_mapping.yaml"
        
        self.config_path = Path(config_path)
        self._raw_config = {}
        self.config = self._load_config()
    
    def _load_config(self) -> Dict[str, Any]:
        """加载配置文件"""
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                config = yaml.safe_load(f)
                logger.info(f"[SensorConfig] 加载配置: {self.config_path}")
                # 保存完整的原始配置，包含sensor_types部分
                self._raw_config = config or {}
                return config.get('sensor_events', {})
        except Exception as e:
            logger.error(f"[SensorConfig] 加载配置失败: {e}")
            self._raw_config = {}
            return {}
    
    def get_action(self, event_name: str, current_state: str) -> Optional[Dict[str, Any]]:
        """
        获取事件在指定状态下的动作配置
        
        Args:
            event_name: 事件名称（如 touch_left_single_tap）
            current_state: 当前状态（如 IDLE, ACTIVATED, EXPLORING）
        
        Returns:
            动作配置字典，找不到返回None
        """
        event_config = self.config.get(event_name)
        if not event_config:
            logger.debug(f"[SensorConfig] 未找到事件配置: {event_name}")
            return None
        
        states = event_config.get('states', {})
        
        # 优先查找当前状态的配置
        if current_state in states:
            logger.debug(f"[SensorConfig] 使用 {current_state} 状态配置: {event_name}")
            return states[current_state]
        
        # 否则查找ANY状态配置（通用配置）
        if 'ANY' in states:
            logger.debug(f"[SensorConfig] 使用 ANY 状态配置: {event_name}")
            return states['ANY']
        
        logger.debug(f"[SensorConfig] 未找到 {event_name} 在 {current_state} 的配置")
        return None
    
    def get_all_events(self) -> list:
        """获取所有已配置的事件名称"""
        return list(self.config.keys())
    
    def reload(self):
        """重新加载配置文件"""
        self.config = self._load_config()
        logger.info("[SensorConfig] 配置已重新加载")
    
    def is_sensor_type_enabled(self, event_name: str) -> bool:
        """
        检查传感器类型是否启用
        
        Args:
            event_name: 事件名称（如 touch_left_single_tap, cliff_left_triggered, tof_obstacle_left）
        
        Returns:
            该传感器类型是否启用
        """
        # 从事件名称前缀推断传感器类型
        sensor_type = self._get_sensor_type_from_event(event_name)
        
        if sensor_type is None:
            # 无法推断类型，默认启用
            logger.debug(f"[SensorConfig] 无法确定 {event_name} 的传感器类型，默认启用")
            return True
        
        # 获取sensor_types配置
        sensor_types = self._raw_config.get('sensor_types', {})
        sensor_config = sensor_types.get(sensor_type, {})
        enabled = sensor_config.get('enabled', True)  # 默认启用
        
        return enabled
    
    def _get_sensor_type_from_event(self, event_name: str) -> Optional[str]:
        """
        从事件名称推断传感器类型
        
        Args:
            event_name: 事件名称
        
        Returns:
            传感器类型（touch, cliff, tof, imu, battery等），无法推断时返回None
        """
        if event_name.startswith('touch_'):
            return 'touch'
        elif event_name.startswith('cliff_'):
            return 'cliff'
        elif event_name.startswith('tof_'):
            return 'tof'
        elif event_name.startswith('imu_'):
            return 'imu'
        elif event_name.startswith('battery_'):
            return 'battery'
        return None

