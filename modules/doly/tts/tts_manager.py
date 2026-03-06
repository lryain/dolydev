"""
TTS管理器
统一的TTS接口，支持多引擎切换和情绪化语音

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""
import logging
from pathlib import Path
from typing import Optional, Dict, Any, Callable
import yaml
import zmq

from .engines import EspeakEngine, EdgeTTSEngine
from .audio_playback_queue import AudioPlaybackQueue

logger = logging.getLogger(__name__)


class TTSManager:
    """TTS管理器"""
    
    def __init__(self, config_path: str = "/home/pi/dolydev/config/tts_settings.yaml"):
        """
        初始化TTS管理器
        
        Args:
            config_path: 配置文件路径
        """
        self.config_path = Path(config_path)
        self.config: Dict[str, Any] = {}
        
        # 引擎字典
        self.engines: Dict[str, Any] = {}
        self.current_engine_name: str = ""
        self.current_engine = None
        
        # 情绪映射
        self.emotion_mapping: Dict[str, str] = {}
        self.use_robot_emotion: bool = True
        self.current_robot_emotion: Optional[str] = None
        
        # 情绪状态提供器（由外部设置）
        self.emotion_provider: Optional[Callable[[], str]] = None
        
        # 音频播放队列
        self.playback_queue: Optional[AudioPlaybackQueue] = None
        
        # ZMQ发布器（用于发送音频播放命令，已被playback_queue替代）
        self.zmq_publisher = None
        self.zmq_endpoint: Optional[str] = None
        
        # 加载配置
        self._load_config()
        
        # 初始化引擎
        self._init_engines()
        
        # 初始化音频播放队列
        self._init_playback_queue()
        
        logger.info("✅ [TTSManager] 初始化完成")
    
    def _load_config(self):
        """加载配置文件"""
        try:
            if not self.config_path.exists():
                logger.warning(f"[TTSManager] 配置文件不存在: {self.config_path}")
                self._use_default_config()
                return
            
            with open(self.config_path, 'r', encoding='utf-8') as f:
                self.config = yaml.safe_load(f) or {}
            
            tts_config = self.config.get('tts', {})
            self.current_engine_name = tts_config.get('default_engine', 'espeak')
            self.emotion_mapping = tts_config.get('emotion_mapping', {})
            self.use_robot_emotion = tts_config.get('use_robot_emotion', True)
            self.zmq_endpoint = tts_config.get('output', {}).get('zmq_endpoint')
            
            logger.info(f"[TTSManager] 配置加载成功，默认引擎: {self.current_engine_name}")
            
        except Exception as e:
            logger.error(f"[TTSManager] 配置加载失败: {e}", exc_info=True)
            self._use_default_config()
    
    def _use_default_config(self):
        """使用默认配置"""
        self.config = {
            'tts': {
                'default_engine': 'espeak',
                'engines': {
                    'espeak': {'enabled': True},
                    'edge-tts': {'enabled': True}
                },
                'emotion_mapping': {
                    'happy': 'happy',
                    'sad': 'sad',
                    'angry': 'angry',
                    'excited': 'excited'
                },
                'use_robot_emotion': True
            }
        }
        self.current_engine_name = 'espeak'
        logger.info("[TTSManager] 使用默认配置")
    
    def _init_engines(self):
        """初始化所有可用的TTS引擎"""
        engines_config = self.config.get('tts', {}).get('engines', {})
        
        # 初始化espeak引擎
        if 'espeak' in engines_config:
            try:
                self.engines['espeak'] = EspeakEngine(engines_config['espeak'])
                logger.info("[TTSManager] espeak 引擎已加载")
            except Exception as e:
                logger.error(f"[TTSManager] espeak 引擎加载失败: {e}")
        
        # 初始化edge-tts引擎
        if 'edge-tts' in engines_config:
            try:
                self.engines['edge-tts'] = EdgeTTSEngine(engines_config['edge-tts'])
                logger.info("[TTSManager] edge-tts 引擎已加载")
            except Exception as e:
                logger.error(f"[TTSManager] edge-tts 引擎加载失败: {e}")
        
        # 设置当前引擎，如果默认引擎不可用，尝试使用第一个可用的引擎
        if not self.switch_engine(self.current_engine_name):
            # 默认引擎不可用，尝试其他可用引擎
            for engine_name in self.engines.keys():
                if self.switch_engine(engine_name):
                    logger.info(f"[TTSManager] 默认引擎不可用，已切换到: {engine_name}")
                    break
    
    def _init_playback_queue(self):
        """初始化音频播放队列"""
        try:
            queue_config = self.config.get('tts', {}).get('playback_queue', {})
            
            if queue_config.get('enabled', True):
                self.playback_queue = AudioPlaybackQueue(queue_config)
                logger.info("[TTSManager] 音频播放队列已初始化")
            else:
                logger.info("[TTSManager] 音频播放队列已禁用")
                
        except Exception as e:
            logger.error(f"[TTSManager] 音频播放队列初始化失败: {e}", exc_info=True)
            self.playback_queue = None
    
    def switch_engine(self, engine_name: str) -> bool:
        """
        切换TTS引擎
        
        Args:
            engine_name: 引擎名称（espeak, edge-tts等）
            
        Returns:
            是否切换成功
        """
        if engine_name not in self.engines:
            logger.error(f"[TTSManager] 引擎不存在: {engine_name}")
            return False
        
        engine = self.engines[engine_name]
        if not engine.is_available():
            logger.error(f"[TTSManager] 引擎不可用: {engine_name}")
            return False
        
        self.current_engine = engine
        self.current_engine_name = engine_name
        logger.info(f"[TTSManager] 已切换到引擎: {engine_name}")
        return True
    
    def speak(
        self, 
        text: str, 
        emotion: Optional[str] = None,
        voice: Optional[str] = None,
        play_now: bool = True,
        **kwargs
    ) -> Optional[Path]:
        """
        合成并播放语音
        
        Args:
            text: 要合成的文本
            emotion: 情绪类型（可选）
            voice: 发音人（可选）
            play_now: 是否立即播放（通过ZMQ发送播放命令）
            **kwargs: 其他引擎特定参数
            
        Returns:
            生成的音频文件路径
        """
        if not self.current_engine:
            logger.error("[TTSManager] 当前没有可用的TTS引擎")
            return None
        
        # 确定情绪
        final_emotion = self._determine_emotion(emotion)
        
        logger.info(f"[TTSManager] 🎤 合成语音: text='{text[:20]}...', emotion={final_emotion}, engine={self.current_engine_name}")
        
        # 调用引擎合成
        audio_file = self.current_engine.speak(
            text=text,
            emotion=final_emotion,
            voice=voice,
            **kwargs
        )
        
        if audio_file and play_now:
            self._play_audio(audio_file)
        
        return audio_file
    
    def _determine_emotion(self, explicit_emotion: Optional[str]) -> Optional[str]:
        """
        确定最终使用的情绪
        
        Args:
            explicit_emotion: 明确指定的情绪
            
        Returns:
            最终情绪
        """
        # 如果明确指定了情绪，使用指定的情绪
        if explicit_emotion:
            return explicit_emotion
        
        # 如果启用了自动使用机器人情绪
        if self.use_robot_emotion:
            # 从情绪提供器获取当前情绪
            if self.emotion_provider:
                try:
                    robot_emotion = self.emotion_provider()
                    # 映射到TTS情绪
                    tts_emotion = self.emotion_mapping.get(robot_emotion)
                    if tts_emotion:
                        logger.debug(f"[TTSManager] 使用机器人情绪: {robot_emotion} -> {tts_emotion}")
                        return tts_emotion
                except Exception as e:
                    logger.error(f"[TTSManager] 获取机器人情绪失败: {e}")
        
        return None
    
    def _play_audio(self, audio_file: Path, priority: Optional[int] = None):
        """
        播放音频文件（通过音频播放队列）
        
        Args:
            audio_file: 音频文件路径
            priority: 播放优先级(0-100)
        """
        # 优先使用音频播放队列
        if self.playback_queue and self.playback_queue.enabled:
            try:
                success = self.playback_queue.enqueue(
                    audio_file=audio_file,
                    priority=priority,
                    metadata={'source': 'tts', 'engine': self.current_engine_name}
                )
                
                if success:
                    logger.info(f"[TTSManager] ✅ 音频已加入播放队列: {audio_file.name}")
                else:
                    logger.error(f"[TTSManager] ❌ 音频加入播放队列失败: {audio_file.name}")
                    
            except Exception as e:
                logger.error(f"[TTSManager] 播放队列入队异常: {e}", exc_info=True)
            return
        
        # 回退到旧的ZMQ发布方式（已废弃）
        logger.warning("[TTSManager] 音频播放队列不可用，使用ZMQ发布方式（已废弃）")
        
        if not self.zmq_endpoint:
            logger.warning("[TTSManager] 未配置ZMQ端点，无法播放音频")
            return
        
        try:
            # 创建ZMQ发布器（如果尚未创建）
            if not self.zmq_publisher:
                context = zmq.Context()
                self.zmq_publisher = context.socket(zmq.PUB)
                self.zmq_publisher.connect(self.zmq_endpoint)
                import time
                time.sleep(0.1)  # 等待连接建立
            
            # 发送播放命令
            import json
            message = {
                'topic': 'audio.play',
                'data': {
                    'file': str(audio_file),
                    'source': 'tts',
                    'priority': priority or 5
                }
            }
            
            self.zmq_publisher.send_string(json.dumps(message))
            logger.info(f"[TTSManager] 已发送音频播放命令: {audio_file.name}")
            
        except Exception as e:
            logger.error(f"[TTSManager] 发送音频播放命令失败: {e}", exc_info=True)
    
    def set_emotion_provider(self, provider: Callable[[], str]):
        """
        设置情绪状态提供器
        
        Args:
            provider: 返回当前机器人情绪的回调函数
        """
        self.emotion_provider = provider
        logger.info("[TTSManager] 已设置情绪提供器")
    
    def get_available_engines(self) -> list:
        """获取可用的引擎列表"""
        return [
            name for name, engine in self.engines.items()
            if engine.is_available()
        ]
    
    def get_voices(self) -> list:
        """获取当前引擎的发音人列表"""
        if self.current_engine:
            return self.current_engine.get_voices()
        return []
    
    def get_emotions(self) -> list:
        """获取当前引擎支持的情绪列表"""
        if self.current_engine:
            return self.current_engine.get_emotions()
        return []
    
    def get_queue_status(self) -> Dict[str, Any]:
        """
        获取音频播放队列状态
        
        Returns:
            队列状态字典
        """
        if self.playback_queue:
            return self.playback_queue.get_status()
        return {'enabled': False, 'message': 'Queue not initialized'}
    
    def clear_queue(self):
        """清空音频播放队列"""
        if self.playback_queue:
            self.playback_queue._clear_queue()
            logger.info("[TTSManager] 已清空播放队列")
    
    def stop_playback(self):
        """停止当前播放"""
        if self.playback_queue:
            self.playback_queue._stop_current()
            logger.info("[TTSManager] 已停止当前播放")
    
    def shutdown(self):
        """关闭TTS管理器，释放资源"""
        logger.info("[TTSManager] 正在关闭...")
        
        # 停止播放队列
        if self.playback_queue:
            self.playback_queue.stop()
        
        # 关闭ZMQ连接
        if self.zmq_publisher:
            self.zmq_publisher.close()
        
        logger.info("[TTSManager] 已关闭")
    
    def __del__(self):
        """析构函数"""
        try:
            self.shutdown()
        except:
            pass
