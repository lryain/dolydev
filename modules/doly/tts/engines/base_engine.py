"""
TTS引擎基类
定义统一的TTS引擎接口

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""
from abc import ABC, abstractmethod
from typing import Optional, Dict, Any
from pathlib import Path


class TTSEngine(ABC):
    """TTS引擎基类"""
    
    def __init__(self, config: Dict[str, Any]):
        """
        初始化TTS引擎
        
        Args:
            config: 引擎配置字典
        """
        self.config = config
        self.enabled = config.get('enabled', True)
        
    @abstractmethod
    def speak(
        self, 
        text: str, 
        emotion: Optional[str] = None,
        voice: Optional[str] = None,
        **kwargs
    ) -> Optional[Path]:
        """
        合成语音
        
        Args:
            text: 要合成的文本
            emotion: 情绪类型（如happy, sad等）
            voice: 发音人/语音（可选，覆盖默认值）
            **kwargs: 其他引擎特定参数
            
        Returns:
            生成的音频文件路径，如果失败返回None
        """
        pass
    
    @abstractmethod
    def get_voices(self) -> list:
        """
        获取可用的发音人列表
        
        Returns:
            发音人列表
        """
        pass
    
    @abstractmethod
    def get_emotions(self) -> list:
        """
        获取支持的情绪类型列表
        
        Returns:
            情绪类型列表
        """
        pass
    
    def is_available(self) -> bool:
        """
        检查引擎是否可用
        
        Returns:
            是否可用
        """
        return self.enabled
