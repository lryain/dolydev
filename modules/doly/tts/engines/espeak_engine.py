"""
espeak TTS引擎实现

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""
import subprocess
import logging
from pathlib import Path
from typing import Optional, Dict, Any
import tempfile
import time

from .base_engine import TTSEngine

logger = logging.getLogger(__name__)


class EspeakEngine(TTSEngine):
    """espeak TTS引擎"""
    
    # 情绪到espeak参数的映射
    EMOTION_PARAMS = {
        'happy': {'speed': 180, 'pitch': 60},
        'sad': {'speed': 120, 'pitch': 40},
        'angry': {'speed': 190, 'pitch': 70},
        'excited': {'speed': 200, 'pitch': 75},
        'calm': {'speed': 140, 'pitch': 45},
        'surprised': {'speed': 210, 'pitch': 80},
        'neutral': {'speed': 150, 'pitch': 50},
    }
    
    def __init__(self, config: Dict[str, Any]):
        super().__init__(config)
        self.default_voice = config.get('default_voice', 'zh')
        self.default_speed = config.get('default_speed', 150)
        self.default_pitch = config.get('default_pitch', 50)
        self.default_volume = config.get('default_volume', 100)
        
        # 检查espeak是否安装
        try:
            subprocess.run(['espeak', '--version'], 
                         capture_output=True, check=True)
            logger.info("[EspeakEngine] espeak 可用")
        except (subprocess.CalledProcessError, FileNotFoundError):
            logger.warning("[EspeakEngine] espeak 未安装或不可用")
            self.enabled = False
    
    def speak(
        self, 
        text: str, 
        emotion: Optional[str] = None,
        voice: Optional[str] = None,
        **kwargs
    ) -> Optional[Path]:
        """
        使用espeak合成语音
        
        Args:
            text: 要合成的文本
            emotion: 情绪类型
            voice: 发音人
            **kwargs: 可选参数 speed, pitch, volume
            
        Returns:
            生成的wav文件路径
        """
        if not self.enabled:
            logger.error("[EspeakEngine] 引擎不可用")
            return None
        
        # 确定参数
        voice_name = voice or self.default_voice
        
        # 根据情绪调整参数
        if emotion and emotion in self.EMOTION_PARAMS:
            params = self.EMOTION_PARAMS[emotion].copy()
        else:
            params = {
                'speed': self.default_speed,
                'pitch': self.default_pitch
            }
        
        # 允许kwargs覆盖
        speed = kwargs.get('speed', params.get('speed', self.default_speed))
        pitch = kwargs.get('pitch', params.get('pitch', self.default_pitch))
        volume = kwargs.get('volume', self.default_volume)
        
        # 生成临时文件
        timestamp = int(time.time() * 1000)
        output_file = Path(tempfile.gettempdir()) / f"espeak_{timestamp}.wav"
        
        try:
            # 构建espeak命令
            cmd = [
                'espeak',
                '-v', voice_name,
                '-s', str(speed),
                '-p', str(pitch),
                '-a', str(volume),
                '-w', str(output_file),
                text
            ]
            
            logger.debug(f"[EspeakEngine] 执行命令: {' '.join(cmd)}")
            
            # 执行合成
            result = subprocess.run(
                cmd,
                capture_output=True,
                text=True,
                timeout=10
            )
            
            if result.returncode != 0:
                logger.error(f"[EspeakEngine] 合成失败: {result.stderr}")
                return None
            
            if not output_file.exists():
                logger.error(f"[EspeakEngine] 输出文件不存在: {output_file}")
                return None
            
            logger.info(f"[EspeakEngine] 合成成功: {output_file}")
            return output_file
            
        except subprocess.TimeoutExpired:
            logger.error("[EspeakEngine] 合成超时")
            return None
        except Exception as e:
            logger.error(f"[EspeakEngine] 合成异常: {e}", exc_info=True)
            return None
    
    def get_voices(self) -> list:
        """获取可用的语音列表"""
        try:
            result = subprocess.run(
                ['espeak', '--voices'],
                capture_output=True,
                text=True,
                check=True
            )
            # 简单解析输出
            voices = []
            for line in result.stdout.split('\n')[1:]:  # 跳过标题行
                if line.strip():
                    parts = line.split()
                    if parts:
                        voices.append(parts[3])  # 语音名称在第4列
            return voices
        except Exception as e:
            logger.error(f"[EspeakEngine] 获取语音列表失败: {e}")
            return [self.default_voice]
    
    def get_emotions(self) -> list:
        """获取支持的情绪类型"""
        return list(self.EMOTION_PARAMS.keys())
