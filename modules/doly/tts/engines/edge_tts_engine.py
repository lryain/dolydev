"""
Edge-TTS引擎实现
使用现有的libs/tts/edge-tts模块

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""
import logging
import asyncio
import time
from pathlib import Path
from typing import Optional, Dict, Any
import tempfile

from .base_engine import TTSEngine

logger = logging.getLogger(__name__)


class EdgeTTSEngine(TTSEngine):
    """Edge-TTS引擎"""
    
    def __init__(self, config: Dict[str, Any]):
        super().__init__(config)
        self.default_voice = config.get('default_voice', 'zh-CN-YunxiaNeural')
        self.emotion_presets = config.get('emotion_presets', {})
        
        # 尝试导入edge-tts
        try:
            import edge_tts
            self.edge_tts = edge_tts
            logger.info("[EdgeTTSEngine] edge-tts 模块可用")
        except ImportError:
            logger.warning("[EdgeTTSEngine] edge-tts 模块未安装")
            self.enabled = False
            self.edge_tts = None
    
    def speak(
        self, 
        text: str, 
        emotion: Optional[str] = None,
        voice: Optional[str] = None,
        **kwargs
    ) -> Optional[Path]:
        """
        使用edge-tts合成语音
        
        Args:
            text: 要合成的文本
            emotion: 情绪类型
            voice: 发音人
            **kwargs: 可选参数 pitch, rate, volume
            
        Returns:
            生成的mp3文件路径
        """
        if not self.enabled:
            logger.error("[EdgeTTSEngine] 引擎不可用")
            return None
        
        # 确定参数
        voice_name = voice or self.default_voice
        
        # 根据情绪获取预设参数
        if emotion and emotion in self.emotion_presets:
            preset = self.emotion_presets[emotion]
            pitch = kwargs.get('pitch', preset.get('pitch', '+0Hz'))
            rate = kwargs.get('rate', preset.get('rate', '+0%'))
            volume = kwargs.get('volume', preset.get('volume', '+0%'))
        else:
            # 默认参数：pitch必须使用Hz格式
            pitch = kwargs.get('pitch', '+0Hz')
            rate = kwargs.get('rate', '+0%')
            volume = kwargs.get('volume', '+0%')
        
        # 生成临时文件
        timestamp = int(time.time() * 1000)
        output_file = Path(tempfile.gettempdir()) / f"edge_tts_{timestamp}.mp3"
        
        try:
            # 异步调用edge-tts
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            
            try:
                loop.run_until_complete(
                    self._synthesize_async(
                        text, voice_name, pitch, rate, volume, output_file
                    )
                )
            finally:
                loop.close()
            
            if not output_file.exists():
                logger.error(f"[EdgeTTSEngine] 输出文件不存在: {output_file}")
                return None
            
            logger.info(f"[EdgeTTSEngine] 合成成功: {output_file}")
            return output_file
            
        except Exception as e:
            logger.error(f"[EdgeTTSEngine] 合成异常: {e}", exc_info=True)
            return None
    
    async def _synthesize_async(
        self, 
        text: str, 
        voice: str,
        pitch: str,
        rate: str,
        volume: str,
        output_file: Path
    ):
        """异步合成方法"""
        communicate = self.edge_tts.Communicate(
            text, 
            voice,
            pitch=pitch,
            rate=rate,
            volume=volume
        )
        
        await communicate.save(str(output_file))
    
    def get_voices(self) -> list:
        """获取可用的发音人列表"""
        if not self.enabled:
            return []
        
        try:
            loop = asyncio.new_event_loop()
            asyncio.set_event_loop(loop)
            
            try:
                voices = loop.run_until_complete(self._get_voices_async())
                # 只返回中文发音人
                chinese_voices = [
                    v['Name'] for v in voices 
                    if 'zh-CN' in v.get('Locale', '')
                ]
                return chinese_voices
            finally:
                loop.close()
                
        except Exception as e:
            logger.error(f"[EdgeTTSEngine] 获取发音人列表失败: {e}")
            return [self.default_voice]
    
    async def _get_voices_async(self):
        """异步获取发音人列表"""
        voices = await self.edge_tts.list_voices()
        return voices
    
    def get_emotions(self) -> list:
        """获取支持的情绪类型"""
        return list(self.emotion_presets.keys())
