"""
EyeEngine 配置加载器

从 YAML 文件加载配置，支持默认配置和用户自定义配置合并

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
from pathlib import Path
from typing import Dict, Any, Optional
import yaml

logger = logging.getLogger(__name__)


class EyeEngineConfigLoader:
    """EyeEngine 配置加载器"""
    
    DEFAULT_CONFIG_FILE = "default_config.yaml"
    
    def __init__(self, config_dir: Optional[str] = None):
        """
        初始化配置加载器
        
        Args:
            config_dir: 配置文件目录，默认为 eyeEngine 模块目录
        """
        if config_dir:
            self.config_dir = Path(config_dir)
        else:
            # 默认为当前模块所在目录
            self.config_dir = Path(__file__).parent
        
        self._config: Dict[str, Any] = {}
        self._loaded = False
    
    def load(self, user_config_path: Optional[str] = None) -> Dict[str, Any]:
        """
        加载配置
        
        Args:
            user_config_path: 用户自定义配置文件路径（可选）
        
        Returns:
            合并后的配置字典
        """
        # 1. 加载默认配置
        default_path = self.config_dir / self.DEFAULT_CONFIG_FILE
        if not default_path.exists():
            logger.warning(f"默认配置文件不存在: {default_path}")
            self._config = self._get_fallback_config()
        else:
            with open(default_path, 'r', encoding='utf-8') as f:
                self._config = yaml.safe_load(f) or {}
            logger.info(f"已加载默认配置: {default_path}")
        
        # 2. 如果提供了用户配置，合并
        if user_config_path:
            user_path = Path(user_config_path)
            if user_path.exists():
                with open(user_path, 'r', encoding='utf-8') as f:
                    user_config = yaml.safe_load(f) or {}
                self._merge_config(self._config, user_config)
                logger.info(f"已加载用户配置: {user_path}")
            else:
                logger.warning(f"用户配置文件不存在: {user_path}")
        
        self._loaded = True
        return self._config
    
    def _merge_config(self, base: Dict[str, Any], override: Dict[str, Any]) -> None:
        """
        递归合并配置（override 覆盖 base）
        
        Args:
            base: 基础配置
            override: 覆盖配置
        """
        for key, value in override.items():
            if key in base and isinstance(base[key], dict) and isinstance(value, dict):
                self._merge_config(base[key], value)
            else:
                base[key] = value
    
    def _get_fallback_config(self) -> Dict[str, Any]:
        """获取后备配置（当默认配置文件不存在时）"""
        return {
            'appearance': {
                'iris_theme': 'CLASSIC',
                'iris_style': 'COLOR_BLUE',
                'left_iris_theme': '',
                'left_iris_style': '',
                'right_iris_theme': '',
                'right_iris_style': '',
                'side_lid_id': 0,
                'top_lid_id': 0,
                'bottom_lid_id': 0,
                'left_lid_id': None,
                'right_lid_id': None,
                'background_type': 'COLOR',
                'background_style': 'COLOR_BLACK',
                'brightness': 8,
                'left_brightness': None,
                'right_brightness': None,
            },
            'default_expression': '',
            'auto_blink': {
                'enabled': True,
                'interval_min': 2.0,
                'interval_max': 6.0,
            },
            'auto_expression_carousel': {
                'enabled': False,
                'expressions': [],
                'duration': 5.0,
                'interval': 2.0,
                'random_order': True,
            },
            'performance': {
                'default_fps': 30,
                'use_mock': False,
            },
            'zmq_service': {
                'command_endpoint': 'ipc:///tmp/doly_eye_cmd.sock',
                'event_endpoint': 'ipc:///tmp/doly_eye_event.sock',
                'timeout_ms': 5000,
            },
            'task_priority': {
                'enabled': True,
                'default_priority': 5,
                'max_priority': 10,
                'min_priority': 1,
            },
            'passive_mode': {
                'enabled': False
            },
            'auto_reset': {
                'enabled': True,
                'delay_ms': 300,
                'expression': '',
                'restore_passive': True,
            }
        }
    
    def get(self, key_path: str, default: Any = None) -> Any:
        """
        获取配置值（支持点分隔的路径）
        
        Args:
            key_path: 配置键路径，如 "appearance.iris_theme"
            default: 默认值
        
        Returns:
            配置值
        """
        if not self._loaded:
            self.load()
        
        keys = key_path.split('.')
        value = self._config
        
        for key in keys:
            if isinstance(value, dict) and key in value:
                value = value[key]
            else:
                return default
        
        return value
    
    @property
    def config(self) -> Dict[str, Any]:
        """获取完整配置"""
        if not self._loaded:
            self.load()
        return self._config


# 全局配置加载器实例
_global_config_loader: Optional[EyeEngineConfigLoader] = None


def get_config_loader(config_dir: Optional[str] = None, 
                     user_config: Optional[str] = None) -> EyeEngineConfigLoader:
    """
    获取全局配置加载器实例（单例）
    
    Args:
        config_dir: 配置目录
        user_config: 用户配置文件路径
    
    Returns:
        配置加载器实例
    """
    global _global_config_loader
    
    if _global_config_loader is None:
        _global_config_loader = EyeEngineConfigLoader(config_dir)
        _global_config_loader.load(user_config)
    
    return _global_config_loader
