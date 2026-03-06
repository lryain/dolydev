"""
Doly 命令映射器

将语音指令映射到动画、动作或直接命令。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import yaml
import logging
from typing import Dict, Any, Optional, List
from dataclasses import dataclass, field
from pathlib import Path

logger = logging.getLogger(__name__)


@dataclass
class CommandAction:
    """命令动作定义"""
    
    action_type: str        # "play_animation", "direct_command", "skill", "dummy"
    animation: str = ""     # 动画文件名（如 happy_1.xml）
    target: str = ""        # 目标模块（eye, drive, audio）
    command: str = ""       # 直接命令名称
    params: Dict[str, Any] = field(default_factory=dict)  # 命令参数
    audio: str = ""         # 关联音频文件
    priority: str = "normal"  # low, normal, high
    interruptible: bool = True  # 是否可被打断
    
    def to_dict(self) -> Dict[str, Any]:
        return {
            'action_type': self.action_type,
            'animation': self.animation,
            'target': self.target,
            'command': self.command,
            'params': self.params,
            'audio': self.audio,
            'priority': self.priority,
            'interruptible': self.interruptible,
        }


class CommandMapper:
    """
    命令映射器
    
    负责：
    - 加载语音指令映射配置
    - 将命令名称映射到具体动作
    - 支持动态更新映射
    """
    
    def __init__(self, config_path: Optional[str] = None):
        """
        初始化命令映射器
        
        Args:
            config_path: 配置文件路径
        """
        self.config_path = config_path or "/home/pi/dolydev/config/voice_command_mapping.yaml"
        
        # 命令映射: command_name -> CommandAction
        self._mappings: Dict[str, CommandAction] = {}
        
        # 分类映射
        self._category_mappings: Dict[str, List[str]] = {}
        
        # 加载配置
        self._load_config()
        
        logger.info(f"[CommandMapper] 初始化完成, 已加载 {len(self._mappings)} 个命令映射")
    
    def _load_config(self) -> None:
        """加载配置文件"""
        try:
            config_file = Path(self.config_path)
            if config_file.exists():
                with open(config_file, 'r', encoding='utf-8') as f:
                    config = yaml.safe_load(f) or {}
                self._parse_config(config)
            else:
                logger.warning(f"[CommandMapper] 配置文件不存在: {self.config_path}")
                self._load_default_mappings()
        except Exception as e:
            logger.error(f"[CommandMapper] 加载配置失败: {e}")
            self._load_default_mappings()
    
    def _parse_config(self, config: Dict[str, Any]) -> None:
        """解析配置"""
        mappings = config.get('mappings', {})
        
        for cmd_name, action_config in mappings.items():
            if isinstance(action_config, dict):
                action = CommandAction(
                    action_type=action_config.get('action', 'dummy'),
                    animation=action_config.get('animation', ''),
                    target=action_config.get('target', ''),
                    command=action_config.get('command', ''),
                    params=action_config.get('params', {}),
                    audio=action_config.get('audio', ''),
                    priority=action_config.get('priority', 'normal'),
                    interruptible=action_config.get('interruptible', True),
                )
            else:
                # 简单格式：只有动画名
                action = CommandAction(
                    action_type='play_animation',
                    animation=str(action_config)
                )
            
            self._mappings[cmd_name] = action
            
            # 分类
            category = action_config.get('category', 'general') if isinstance(action_config, dict) else 'general'
            if category not in self._category_mappings:
                self._category_mappings[category] = []
            self._category_mappings[category].append(cmd_name)
    
    def _load_default_mappings(self) -> None:
        """加载默认映射"""
        logger.info("[CommandMapper] 使用默认映射")
        
        default_mappings = {
            # 唤醒类
            'wakeup_detected': CommandAction(
                action_type='play_animation',
                animation='wakeup.xml',
                priority='high'
            ),
            'cmd_iHelloDoly': CommandAction(
                action_type='play_animation',
                animation='happy_1.xml',
                priority='high'
            ),
            'cmd_iInterrupt': CommandAction(
                action_type='direct_command',
                target='system',
                command='interrupt',
                priority='high'
            ),
            
            # 休眠类
            'cmd_iShutup': CommandAction(
                action_type='play_animation',
                animation='sleep.xml',
                priority='normal'
            ),
            'cmd_StaSleep': CommandAction(
                action_type='play_animation',
                animation='sleep_2.xml',
                priority='normal'
            ),
            
            # 动作类
            'cmd_ActDance': CommandAction(
                action_type='play_animation',
                animation='salsa.xml',
                priority='normal'
            ),
            'cmd_ActSing': CommandAction(
                action_type='play_animation',
                animation='happy_2.xml',
                priority='normal'
            ),
            'cmd_ActTellJoke': CommandAction(
                action_type='play_animation',
                animation='compliment_1.xml',
                priority='normal'
            ),
            
            # 眼睛控制类
            'cmd_EyeBlink': CommandAction(
                action_type='direct_command',
                target='eye',
                command='blink',
                params={'count': 1, 'eye': 'both'}
            ),
            'cmd_EyeBlinkL': CommandAction(
                action_type='direct_command',
                target='eye',
                command='blink',
                params={'count': 1, 'eye': 'left'}
            ),
            'cmd_EyeBlinkR': CommandAction(
                action_type='direct_command',
                target='eye',
                command='blink',
                params={'count': 1, 'eye': 'right'}
            ),
            'cmd_EyeCircle': CommandAction(
                action_type='direct_command',
                target='eye',
                command='circle',
                params={'direction': 'cw', 'cycles': 1}
            ),
            
            # 手臂类
            'cmd_ArmRise': CommandAction(
                action_type='play_animation',
                animation='hand_up.xml',
                priority='normal'
            ),
            'cmd_ArmRiseL': CommandAction(
                action_type='play_animation',
                animation='hand_up_l.xml',
                priority='normal'
            ),
            'cmd_ArmRiseR': CommandAction(
                action_type='play_animation',
                animation='hand_up_r.xml',
                priority='normal'
            ),
            
            # 移动类
            'cmd_ActForward': CommandAction(
                action_type='direct_command',
                target='drive',
                command='move',
                params={'direction': 'forward', 'speed': 50, 'duration': 1000}
            ),
            'cmd_ActBackward': CommandAction(
                action_type='direct_command',
                target='drive',
                command='move',
                params={'direction': 'backward', 'speed': 50, 'duration': 1000}
            ),
            'cmd_ActTurnLeft': CommandAction(
                action_type='direct_command',
                target='drive',
                command='turn',
                params={'direction': 'left', 'angle': 90}
            ),
            'cmd_ActTurnRight': CommandAction(
                action_type='direct_command',
                target='drive',
                command='turn',
                params={'direction': 'right', 'angle': 90}
            ),
            'cmd_ActStop': CommandAction(
                action_type='direct_command',
                target='drive',
                command='stop',
                params={}
            ),
        }
        
        self._mappings = default_mappings
    
    def get_action(self, command_name: str) -> Optional[CommandAction]:
        """
        获取命令对应的动作
        
        Args:
            command_name: 命令名称（如 wakeup_detected, cmd_ActDance）
            
        Returns:
            CommandAction 或 None
        """
        # 直接查找
        if command_name in self._mappings:
            action = self._mappings[command_name]
            logger.debug(f"[CommandMapper] 找到映射: {command_name} -> {action.action_type}")
            return action

        # 通用前缀剥离查找（语音、传感器、IO）
        prefixes = ['event.audio.', 'sensor.', 'io.', 'event.sensor.']
        for prefix in prefixes:
            if command_name.startswith(prefix):
                short_name = command_name.replace(prefix, '')
                if short_name in self._mappings:
                    return self._mappings[short_name]

        # 未找到，返回 dummy action
        logger.warning(f"[CommandMapper] 未找到命令映射: {command_name}，使用 dummy")
        return CommandAction(
            action_type='dummy',
            params={'original_command': command_name}
        )
    
    def add_mapping(self, command_name: str, action: CommandAction) -> None:
        """
        添加或更新映射
        
        Args:
            command_name: 命令名称
            action: 动作定义
        """
        self._mappings[command_name] = action
        logger.debug(f"[CommandMapper] 添加映射: {command_name}")
    
    def remove_mapping(self, command_name: str) -> bool:
        """
        移除映射
        
        Args:
            command_name: 命令名称
            
        Returns:
            是否成功
        """
        if command_name in self._mappings:
            del self._mappings[command_name]
            return True
        return False
    
    def get_all_commands(self) -> List[str]:
        """获取所有已映射的命令"""
        return list(self._mappings.keys())
    
    def get_commands_by_category(self, category: str) -> List[str]:
        """获取指定分类的命令"""
        return self._category_mappings.get(category, [])
    
    def reload_config(self) -> None:
        """重新加载配置"""
        self._mappings.clear()
        self._category_mappings.clear()
        self._load_config()
        logger.info(f"[CommandMapper] 配置已重新加载, 共 {len(self._mappings)} 个映射")
    
    def save_config(self) -> bool:
        """保存当前映射到配置文件"""
        try:
            config = {
                'mappings': {
                    name: action.to_dict()
                    for name, action in self._mappings.items()
                }
            }
            
            with open(self.config_path, 'w', encoding='utf-8') as f:
                yaml.dump(config, f, allow_unicode=True, default_flow_style=False)
            
            logger.info(f"[CommandMapper] 配置已保存到 {self.config_path}")
            return True
            
        except Exception as e:
            logger.error(f"[CommandMapper] 保存配置失败: {e}")
            return False


if __name__ == '__main__':
    # 简单测试
    logging.basicConfig(level=logging.DEBUG, 
                        format='[%(asctime)s] [%(levelname)s] %(message)s')
    
    mapper = CommandMapper()
    
    # 测试获取映射
    action = mapper.get_action('wakeup_detected')
    print(f"wakeup_detected -> {action}")
    
    action = mapper.get_action('cmd_ActDance')
    print(f"cmd_ActDance -> {action}")
    
    action = mapper.get_action('unknown_command')
    print(f"unknown_command -> {action}")
    
    # 打印所有命令
    print(f"\n所有命令: {mapper.get_all_commands()}")
