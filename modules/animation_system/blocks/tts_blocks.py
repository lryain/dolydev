"""
TTS 语音合成块

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from typing import Any, Dict, List, Optional
import logging
import asyncio

from .base_block import BaseBlock
from ..hardware_interfaces import HardwareInterfaces
from ..parser import AnimationBlock

logger = logging.getLogger(__name__)


class TTSBlock(BaseBlock):
    """TTS 语音合成块"""
    
    # 情绪参数映射表
    EMOTION_PARAMS = {
        "neutral": {"pitch": "+0Hz", "rate": "+0%", "volume": "+0%", "voice": "zh-CN-YunxiaNeural"},
        "happy": {"pitch": "+5Hz", "rate": "+10%", "volume": "+5%", "voice": "zh-CN-XiaoxiaoNeural"},
        "excited": {"pitch": "+10Hz", "rate": "+20%", "volume": "+10%", "voice": "zh-CN-XiaoxiaoNeural"},
        "sad": {"pitch": "-10Hz", "rate": "-15%", "volume": "-5%", "voice": "zh-CN-YunxiaNeural"},
        "angry": {"pitch": "+8Hz", "rate": "+15%", "volume": "+15%", "voice": "zh-CN-YunyangNeural"},
        "gentle": {"pitch": "-3Hz", "rate": "-10%", "volume": "-3%", "voice": "zh-CN-XiaoyiNeural"},
        "surprised": {"pitch": "+15Hz", "rate": "+5%", "volume": "+8%", "voice": "zh-CN-XiaoxiaoNeural"},
        "scared": {"pitch": "+12Hz", "rate": "+25%", "volume": "-5%", "voice": "zh-CN-XiaoyiNeural"},
        "cute": {"pitch": "+8Hz", "rate": "-5%", "volume": "+3%", "voice": "zh-CN-XiaoyiNeural"},
        "calm": {"pitch": "-5Hz", "rate": "-10%", "volume": "-3%", "voice": "zh-CN-YunjianNeural"},
    }
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['BaseBlock']]] = None):
        super().__init__('tts', fields, statements)
        
        # 必选字段
        self.text = self.get_field('text', '')
        
        # 情绪预设
        self.emotion = self.get_field('emotion', 'neutral')
        emotion_config = self.EMOTION_PARAMS.get(self.emotion, self.EMOTION_PARAMS['neutral'])
        
        # 语音参数（字段优先于情绪预设）
        self.voice = self.get_field('voice', emotion_config['voice'])
        self.pitch = self.get_field('pitch', emotion_config['pitch'])
        self.rate = self.get_field('rate', emotion_config['rate'])
        self.volume = self.get_field('volume', emotion_config['volume'])
        
        # 播放控制（需要转换字符串到布尔值）
        self.start = self.get_field('start', 0)
        play_value = self.get_field('play', True)
        self.play = self._to_bool(play_value)
        self.play_mode = self.get_field('play_mode', 'audio_player')
        
        # 保存配置
        self.save_dir = self.get_field('save_dir', None)
        self.filename = self.get_field('filename', None)
        
        # 完成后执行的块
        statements = statements or {}
        self.complete_blocks = statements.get('complete_statement', [])
    
    def _to_bool(self, value) -> bool:
        """将字段值转换为布尔值"""
        if isinstance(value, bool):
            return value
        if isinstance(value, str):
            return value.lower() in ('true', '1', 'yes')
        return bool(value)
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行 TTS 语音合成"""
        if not self.text:
            logger.warning("[TTSBlock] 缺少 text 字段，跳过")
            return
        
        if not interfaces.tts:
            logger.warning("[TTSBlock] TTS interface not available")
            return
        
        # 起始延迟
        if self.start > 0:
            await asyncio.sleep(self.start / 1000.0)
        
        # 构建保存配置
        save_config = {}
        if self.save_dir:
            save_config['dir'] = self.save_dir
        if self.filename:
            save_config['filename'] = self.filename
        
        # 构建 TTS 请求
        request = {
            "action": "tts.synthesize",
            "text": self.text,
            "voice": self.voice,
            "pitch": self.pitch,
            "rate": self.rate,
            "volume": self.volume,
            "play": self.play,
            "play_mode": self.play_mode,
            "format": "wav",
        }
        
        if save_config:
            request["save"] = save_config
        
        logger.info(f"[TTSBlock] 发送请求: text={self.text[:30]}..., emotion={self.emotion}, "
                   f"play={self.play}, mode={self.play_mode}")
        
        try:
            # 发送 TTS 请求
            response = await interfaces.tts.synthesize(request)
            
            if response.get('ok'):
                path = response.get('path')
                playback = response.get('playback', {})
                play_ok = playback.get('sent') if isinstance(playback, dict) else False
                
                logger.info(f"[TTSBlock] ✅ 合成成功: {path}")
                if self.play:
                    if play_ok:
                        logger.info(f"[TTSBlock] 📢 播放指令已发送到 {self.play_mode}")
                    else:
                        logger.warning(f"[TTSBlock] ⚠️ 合成成功但播放失败: {playback.get('error', '未知原因')}")
            else:
                error = response.get('error', {})
                logger.error(f"[TTSBlock] ❌ 合成失败: {error.get('message')}")
        except Exception as e:
            logger.error(f"[TTSBlock] ❌ 请求异常: {e}")
            raise
    
    def get_complete_blocks(self) -> List['BaseBlock']:
        """获取完成后执行的块"""
        return self.complete_blocks
    
    def validate(self) -> bool:
        """验证参数有效性"""
        return bool(self.text)


class TTSEmotionBlock(TTSBlock):
    """TTS 情绪快捷块（简化版）"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['BaseBlock']]] = None):
        # 继承 TTSBlock，使用相同的初始化逻辑
        super().__init__(fields, statements)
        self.block_type = 'tts_emotion'
