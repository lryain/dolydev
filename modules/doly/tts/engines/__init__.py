"""
TTS引擎模块

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""
from .base_engine import TTSEngine
from .espeak_engine import EspeakEngine
from .edge_tts_engine import EdgeTTSEngine

__all__ = ['TTSEngine', 'EspeakEngine', 'EdgeTTSEngine']
