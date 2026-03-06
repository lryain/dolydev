"""
Doly Daemon - 主程序

整合所有模块，实现完整的 Doly 机器人控制系统。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import sys
import time
import asyncio
import signal
import logging
import threading
from pathlib import Path
from typing import Optional, Dict, Any

# 设置路径
repo_root = str(Path(__file__).resolve().parents[2])
sys.path.insert(0, repo_root)

import zmq
from modules.doly.state_machine import DolyState, DolyMode, DolyStateMachine
from modules.doly.event_bus import EventBus, EventType, DolyEvent, ZMQEventSubscriber
# ★★★ 移除 CommandMapper 导入，统一使用 VoiceCommandManager ★★★
# from modules.doly.command_mapper import CommandMapper
from modules.doly.animation_integration import get_animation_integration
from modules.doly.gesture_recognizer import GestureRecognizer
from modules.doly.event_throttler import EventThrottler
from modules.doly.sensor_config import SensorEventConfig
from modules.doly.blockly_runtime import BlocklyRuntimeBinding
from modules.doly.exploration_manager import ExplorationManager
from modules.doly.gesture_interaction_manager import GestureInteractionManager

# ★★★ 导入所有管理器 ★★★
from modules.doly.clients import EyeEngineClient, WidgetServiceClient
# Use the C++ pybind-based EyeEngineClient implementation from libs for replacement.
# from libs.EyeEngineClient import EyeEngineClient
# from modules.doly.clients import WidgetServiceClient
from modules.doly.managers import (
    WidgetManager,
    VoiceCommandManager,
    VoiceCommandHandlers,
    SensorEventManager,
    AnimationManager,
    StateBehaviorManager,
    TimerEventManager,
    XiaozhiEmotionManager,
    FaceRecoManager
)
from modules.doly.managers.audio_volume_manager import AudioVolumeManager
from modules.doly.managers.xiaozhi_manager import XiaozhiManager

# TaskEngine
from modules.taskEngine import TaskEngine

# TOF 集成扩展
from modules.doly.tof_daemon_integration import extend_daemon_with_tof

# 配置日志
logging.basicConfig(
    level=logging.INFO,
    format='[%(asctime)s] [%(name)s] [%(levelname)s] %(message)s',
    datefmt='%H:%M:%S'
)
logger = logging.getLogger(__name__)


class DolyDaemon:
    """
    Doly 主控制器
    
    职责：
    - 管理状态机和模式切换
    - 协调事件总线
    - 集成语音指令处理
    - 集成 Blockly 执行
    - 实现自主行为
    """
    
    def __init__(self, config_dir: Optional[str] = None):
        """
        初始化 Daemon
        
        Args:
            config_dir: 配置目录
        """
        self.config_dir = Path(config_dir or "/home/pi/dolydev/config")
        # 载入全局配置字典（只加载需要的配置片段，供扩展使用）
        self.config = {}
        try:
            import yaml
            tof_cfg_path = self.config_dir / 'tof_integration.yaml'
            if tof_cfg_path.exists():
                with open(tof_cfg_path, 'r', encoding='utf-8') as f:
                    raw = yaml.safe_load(f) or {}
                    # 兼容顶层可能包裹在 'tof_integration' 键下
                    if 'tof_integration' in raw:
                        self.config['tof_integration'] = raw.get('tof_integration', {})
                    else:
                        # 如果文件以直接配置形式存在，作为整个段落
                        self.config['tof_integration'] = raw
            else:
                # 默认值
                self.config['tof_integration'] = {
                    'enabled': True
                }
        except Exception:
            self.config['tof_integration'] = {'enabled': True}
        
        # ★ 新增：创建主 event loop（用于异步操作）
        try:
            self.loop = asyncio.get_event_loop()
        except RuntimeError:
            self.loop = asyncio.new_event_loop()
            asyncio.set_event_loop(self.loop)
        
        # 核心组件
        self.state_machine = DolyStateMachine()
        self.event_bus = EventBus()
        # ★ 为 event_bus 设置 daemon 引用，以便 WebSocket 访问
        self.event_bus.daemon = self
        # ★★★ 移除 CommandMapper，统一使用 VoiceCommandManager ★★★
        # self.command_mapper = CommandMapper()
        
        # ★★★ 初始化所有 ZMQ 客户端 ★★★
        self.eye_client = EyeEngineClient()
        self.widget_client = WidgetServiceClient()
        
        # ★★★ 初始化所有管理器 ★★★
        self.widget_manager = WidgetManager(self.widget_client)
        self.animation_manager = AnimationManager()
        self.sensor_manager = SensorEventManager(str(self.config_dir / "sensor_event_mapping.yaml"))
        self.state_behavior_manager = StateBehaviorManager(str(self.config_dir / "state_behaviors.yaml"))
        self.voice_manager = VoiceCommandManager(str(self.config_dir / "voice_command_mapping.yaml"))
        
        # ★★★ 初始化音量管理器 ★★★
        audio_config = {
            'max_volume': 100,
            'min_volume': 0,
            'step': 10,
            'default_volume': 80,
        }
        self.audio_volume_manager = AudioVolumeManager(config=audio_config)
        
        # ★★★ 传递 animation_manager 和 daemon 引用给 handlers ★★★
        self.voice_handlers = VoiceCommandHandlers(
            widget_manager=self.widget_manager,
            animation_manager=self.animation_manager,
            daemon=self
        )
        self.timer_manager = TimerEventManager(str(self.config_dir / "timer_event_mapping.yaml"))
        # ★★★ 新增：小智情绪管理器 ★★★
        self.xiaozhi_emotion_manager = XiaozhiEmotionManager(str(self.config_dir / "xiaozhi_emotion_mapping.yaml"))
        
        # ★★★ 新增：TTS管理器 ★★★
        from modules.doly.tts import TTSManager
        self.tts_manager = TTSManager(str(self.config_dir / "tts_settings.yaml"))
        
        # ★★★ 新增：人脸识别管理器 ★★★
        self.face_reco_manager = FaceRecoManager(str(self.config_dir / "face_reco_settings.yaml"))
        
        # ★★★ 新增：Vision 模式管理器 ★★★
        from modules.doly.managers.vision_mode_manager import VisionModeManager
        self.vision_mode_manager = VisionModeManager(
            zmq_publisher=None,  # 稍后在 EventBus 启动后设置
            default_timeout=30
        )
        
        # ★★★ 新增：小智管理器 ★★★
        self.xiaozhi_manager = XiaozhiManager(self.event_bus)
        
        # ★★★ 新增：TaskEngine ★★★
        self.task_engine = TaskEngine(str(self.config_dir / "intent_action_mapping.yaml"))
        
        # ★★★ 设置管理器之间的交叉引用 ★★★
        self.animation_manager.set_eye_client(self.eye_client)
        self.sensor_manager.set_state_provider(lambda: self.state_machine.current_state)
        self.timer_manager.set_animation_manager(self.animation_manager)
        self.sensor_manager.set_state_provider(lambda: self.state_machine.current_state)
        # ★★★ 设置小智情绪管理器的依赖 ★★★
        self.xiaozhi_emotion_manager.set_animation_manager(self.animation_manager)
        self.xiaozhi_emotion_manager.set_state_provider(lambda: self.state_machine.current_state)
        
        # ★★★ 设置TTS管理器的情绪提供器 ★★★
        self.tts_manager.set_emotion_provider(lambda: self.xiaozhi_emotion_manager.get_current_emotion())
        
        # ★★★ 设置人脸识别管理器的依赖 ★★★
        self.face_reco_manager.daemon = self  # ★ 设置 daemon 引用
        self.face_reco_manager.set_animation_manager(self.animation_manager)
        self.face_reco_manager.set_eye_client(self.eye_client)
        self.face_reco_manager.set_state_provider(lambda: self.state_machine.current_state)
        # ZMQ Publisher 会在 initialize() 中 EventBus 启动后设置
        self.face_reco_manager.set_vision_mode_manager(self.vision_mode_manager)  # 设置模式管理器
        # 设置TTS客户端（现在是TTS管理器）
        self.face_reco_manager.tts_client = self.tts_manager
        self.face_reco_manager.on_face_recognized = self._handle_face_recognized_state
        self.face_reco_manager.on_new_face = self._handle_face_new_state
        self.face_reco_manager.on_all_faces_lost = self._handle_all_faces_lost_state
        
        # 传感器事件处理（旧系统，保留兼容）
        self.gesture_recognizer = GestureRecognizer(history_window=2.0)
        self.event_throttler = EventThrottler()
        self.sensor_config = SensorEventConfig(
            str(self.config_dir / "sensor_event_mapping.yaml")
        )
        
        # 动画集成（旧系统，保留兼容）
        self.animation_integration = None
        
        # ★ 新增：Blockly 运行时
        self.blockly_runtime: Optional[BlocklyRuntimeBinding] = None
        
        # ★ 新增：探索模式管理器
        self.exploration_manager: Optional[ExplorationManager] = None
        
        # ★ 新增：手势互动管理器
        self.gesture_interaction_manager: Optional[GestureInteractionManager] = None
        
        # 运行状态
        self._running = False
        self._main_thread: Optional[threading.Thread] = None
        # 状态行为配置（加载自 config/state_behaviors.yaml）
        self.state_behaviors = {}
        
        # ZMQ 事件订阅器
        self._serial_subscriber: Optional[ZMQEventSubscriber] = None
        self._sensor_subscriber: Optional[ZMQEventSubscriber] = None
        self._widget_subscriber: Optional[ZMQEventSubscriber] = None
        self._vision_subscriber: Optional[ZMQEventSubscriber] = None
        
        # 统计信息
        self._event_count = 0
        self._command_count = 0
        # 记录最后一次与主人的交互时间（用于非交互超时进入短睡）
        self.last_interaction_time = time.time()
        # 如果正在短睡，此字段记录短睡结束的时间戳（秒）
        self._nap_end_time: Optional[float] = None
        # 是否静音（禁止通过 actions 表达活泼/吵闹）
        self.actions_muted = False
        # 防止在_on_state_changed中因回退导致递归循环的标志
        self._suppress_state_change_revert = False
        # Snooze 状态追踪（基于 seq 动画完成事件）
        self._snooze_in_progress = False  # 是否正在播放 snooze 动画
        self._snooze_overlay_id: Optional[str] = None  # 当前 snooze 动画的 overlay_id

        # ★★★ 新增：语音命令去重机制（防止音频反馈导致的死循环）★★★
        self._last_voice_command: Optional[str] = None  # 上一条语音命令
        self._last_voice_command_time: float = 0.0  # 上一条命令的时间戳
        self._voice_command_debounce_seconds: float = 3.0  # 去重时间窗口（秒）

        # ★★★ 新增：命令状态锁定机制（防止长时任务被重复触发）★★★
        self._command_locks: Dict[str, float] = {}  # {command_name: unlock_timestamp}
        self._command_lock_config: Dict[str, float] = {
            # 人脸识别命令需要锁定30秒（或直到完成）
            'cmd_ActWhoami': 30.0,
            'cmd_RegisterFace': 60.0,
            'cmd_UpdateFace': 60.0,
            'cmd_DeleteFace': 30.0,
            'cmd_ActTakePhoto': 10.0,
            'cmd_ActTakeVideo': 30.0,
        }

        # 共享ZMQ上下文，避免每次创建
        self._zmq_ctx = zmq.Context.instance()
        
        logger.info("🤖 [Daemon] 初始化完成")
    
    def _register_handlers(self):
        """注册所有处理器"""
        # ★★★ 注册所有语音指令处理器到 VoiceCommandManager ★★★
        self.voice_manager.register_handler('widget_control', self.voice_handlers.handle_widget_control)
        self.voice_manager.register_handler('timer_control', self.voice_handlers.handle_timer_control)
        self.voice_manager.register_handler('play_animation', self.voice_handlers.handle_play_animation)
        self.voice_manager.register_handler('direct_command', self.voice_handlers.handle_direct_command)
        self.voice_manager.register_handler('skill', self.voice_handlers.handle_skill)
        self.voice_manager.register_handler('photo', self.voice_handlers.handle_photo)
        self.voice_manager.register_handler('confirmation', self.voice_handlers.handle_confirmation)  # ★ 注册确认命令处理器
        self.voice_manager.register_handler('face_management', self.voice_handlers.handle_face_management)  # ★ 注册人脸管理处理器
        
        # 传感器事件处理器
        self.sensor_manager.register_handler('action', self._handle_sensor_action)
        self.sensor_manager.register_handler('tof', self._handle_tof_event)
        self.sensor_manager.register_handler('imu', self._handle_imu_event)
        
        # ★★★ 注册小智管理器回调 ★★★
        self.xiaozhi_manager.register_emotion_callback(self._handle_xiaozhi_emotion)
        self.xiaozhi_manager.register_action_callback(self._handle_xiaozhi_action)
        self.xiaozhi_manager.register_intent_callback(self._handle_xiaozhi_intent)
        
        # ★★★ 注册 TaskEngine 接口 ★★★
        self.task_engine.register_interface('eye', self.eye_client)
        self.task_engine.register_interface('widget', self.widget_client)
        if hasattr(self, 'animation_integration') and self.animation_integration:
            self.task_engine.register_interface('animation', self.animation_integration)
        
        logger.info("✅ [Daemon] 所有处理器已注册")
    
    def initialize(self) -> bool:
        """初始化所有系统"""
        try:
            # 启动事件总线
            if not self.event_bus.start():
                logger.error("[Daemon] 事件总线启动失败")
                return False
            
            # ★★★ EventBus 启动后，创建 ZMQ 命令发布器包装并注入到管理器 ★★★
            if hasattr(self.event_bus, '_zmq_pub') and self.event_bus._zmq_pub:
                from modules.doly.utils.zmq_command_publisher import ZMQCommandPublisher
                zmq_command_pub = ZMQCommandPublisher(self.event_bus._zmq_pub)
                
                # 注入到 VisionModeManager
                self.vision_mode_manager.set_zmq_publisher(self.event_bus._zmq_pub)
                logger.info("[Daemon] ✅ VisionModeManager 已连接到 EventBus ZMQ Publisher")
                
                # ★★★ 新增：设置初始模式（带延迟和重试，解决 ZMQ 消息丢失问题）★★★
                def set_initial_vision_mode():
                    try:
                        time.sleep(2.0) # 等待 FaceReco 订阅线程就绪
                        # 从配置中获取默认模式，注意路径匹配 face_reco_settings.yaml
                        face_reco_config = self.face_reco_manager.config.get('face_recognition', {})
                        mode_config = face_reco_config.get('mode', {})
                        initial_mode = mode_config.get('default', 'IDLE').upper()
                        
                        logger.info(f"[Daemon] 📤 同步初始 Vision 模式: {initial_mode}")
                        self.vision_mode_manager.set_mode(initial_mode, repeat=3)
                    except Exception as e:
                        logger.error(f"[Daemon] 同步初始 Vision 模式失败: {e}")
                
                threading.Thread(target=set_initial_vision_mode, name="Vision-Init-Sync", daemon=True).start()
                
                # 注入到 FaceRecoManager
                self.face_reco_manager.set_zmq_publisher(zmq_command_pub)
                logger.info("[Daemon] ✅ FaceRecoManager 已连接到 ZMQ 命令发布器")
            else:
                logger.warning("[Daemon] ⚠️ EventBus ZMQ Publisher 不可用，Vision 功能将无法工作")
            
            # 注册状态变化回调
            self.state_machine.set_state_change_callback(self._on_state_changed)
            
            # ★★★ 注册所有处理器 ★★★
            self._register_handlers()
            
            # 订阅关键事件
            self._setup_event_subscriptions()
            
            # 启动 ZMQ 订阅器
            self._start_zmq_subscribers()
            
            # ★ 新增：初始化 Blockly 运行时
            self._setup_blockly_runtime()
            
            # ★ 新增：初始化探索管理器 (改为延迟初始化到状态转换时)
            # self._setup_exploration_manager()  # 移到状态转换时初始化，此时 animation_integration 已经准备好
            
            # ★ 新增：初始化手势互动管理器 (改为延迟初始化到状态转换时)
            # self._setup_gesture_interaction_manager()  # 移到状态转换时初始化，此时 animation_integration 已经准备好
            
            # 初始化动画集成（异步）
            try:
                self._setup_animation_integration()
            except Exception as e:
                logger.warning(f"⚠️ [Daemon] 动画系统初始化失败，将继续运行: {e}")

            # 加载状态行为配置（可选）
            try:
                import yaml
                cfg_path = self.config_dir / "state_behaviors.yaml"
                if cfg_path.exists():
                    with open(cfg_path, 'r', encoding='utf-8') as f:
                        self.state_behaviors = yaml.safe_load(f) or {}
                    logger.info(f"✅ [Daemon] 加载状态行为配置: {cfg_path}")
                    # 应用当前状态的 LED（如果配置存在）
                    try:
                        cur = self.state_machine.current_state.value
                        self._apply_led_for_state(cur)
                    except Exception:
                        pass
                else:
                    logger.info(f"[Daemon] 未找到状态行为配置: {cfg_path}, 使用内置逻辑")
            except Exception as e:
                logger.warning(f"⚠️ [Daemon] 无法加载状态行为配置: {e}")

            # 解析 sleep schedule（如果存在）
            self._sleep_schedules = []
            try:
                sleep_cfg = self.state_behaviors.get('sleep') if isinstance(self.state_behaviors, dict) else None
                if sleep_cfg and isinstance(sleep_cfg, dict):
                    for seg in sleep_cfg.get('schedule', []):
                        start = seg.get('start')
                        end = seg.get('end')
                        if start and end:
                            # 保存为 (HH:MM, HH:MM)
                            self._sleep_schedules.append((start, end))
                    if self._sleep_schedules:
                        logger.info(f"[Daemon] 已加载睡眠时段: {self._sleep_schedules}")
            except Exception:
                logger.debug("[Daemon] 无睡眠时段配置或解析失败")

            # 注册 SIGHUP 信号以支持热加载 state_behaviors.yaml
            try:
                def _sighup_handler(signum, frame):
                    logger.info("[Daemon] 收到 SIGHUP，重新加载状态行为配置")
                    try:
                        self.reload_state_behaviors()
                    except Exception as e:
                        logger.error(f"[Daemon] 热加载失败: {e}")

                signal.signal(signal.SIGHUP, _sighup_handler)
                logger.info("[Daemon] 已注册 SIGHUP 配置热加载")
            except Exception as e:
                logger.warning(f"[Daemon] 无法注册 SIGHUP 处理: {e}")
            
            logger.info("✅ [Daemon] 初始化成功")
            
            # ★★★ 启动 TaskEngine ★★★
            if not self.task_engine.start():
                logger.warning("⚠️ [Daemon] TaskEngine 启动失败，但不影响主流程")
            else:
                logger.info("✅ [Daemon] TaskEngine 已启动")
            
            # 在初始化完成后同步启动 TOF 集成（如果可用）
            try:
                # 动态为当前类扩展 TOF 方法（如果尚未扩展）
                if not hasattr(self, 'init_tof_integration'):
                    extend_daemon_with_tof(self.__class__)

                # ★ 修复：使用同步方式初始化TOF，避免事件循环问题
                try:
                    import asyncio
                    # 在新的事件循环中同步执行TOF初始化
                    loop = asyncio.new_event_loop()
                    asyncio.set_event_loop(loop)
                    try:
                        loop.run_until_complete(self.init_tof_integration())
                        logger.info("✅ [Daemon] TOF 集成初始化完成")
                    finally:
                        loop.close()
                        # 恢复主事件循环
                        asyncio.set_event_loop(self.loop)
                except Exception as e:
                    logger.warning(f"⚠️ [Daemon] TOF 集成初始化失败: {e}")
            except Exception as e:
                logger.warning(f"⚠️ [Daemon] 无法启动 TOF 集成: {e}")

            return True
            
        except Exception as e:
            logger.error(f"❌ [Daemon] 初始化失败: {e}")
            return False
    
    def _setup_animation_integration(self) -> None:
        """设置动画集成"""
        # 在线程中异步初始化动画系统
        def init_animation():
            try:
                import asyncio
                # 为此线程创建新的 event loop
                loop = asyncio.new_event_loop()
                asyncio.set_event_loop(loop)
                
                try:
                    # 异步获取动画集成实例
                    anim_integration = loop.run_until_complete(get_animation_integration())
                    if anim_integration and anim_integration._initialized:
                        self.animation_integration = anim_integration
                        # ★★★ 修复：设置 AnimationManager 的 animation_integration ★★★
                        self.animation_manager.set_animation_integration(anim_integration)
                        logger.info("✅ [Daemon] 动画系统初始化完成")
                    else:
                        logger.warning("⚠️ [Daemon] 动画系统初始化未完成")
                finally:
                    loop.close()
                    
            except Exception as e:
                logger.error(f"[Daemon] 动画系统初始化失败: {e}")
        
        # 在后台线程中初始化，避免阻塞
        thread = threading.Thread(target=init_animation, name="Animation-Init", daemon=True)
        thread.start()
    
    def _setup_event_subscriptions(self) -> None:
        """设置事件订阅"""
        # 订阅语音事件
        self.event_bus.subscribe(
            [EventType.VOICE_WAKEUP, EventType.VOICE_COMMAND],
            self._on_voice_event
        )
        
        # 订阅触摸事件
        self.event_bus.subscribe(
            [EventType.TOUCH_PRESSED],
            self._on_touch_event
        )
        
        # 订阅传感器事件
        self.event_bus.subscribe(
            [EventType.CLIFF_DETECTED, EventType.OBSTACLE_DETECTED, EventType.BATTERY_LOW],
            self._on_sensor_event
        )
        
        # 订阅 Blockly 事件
        self.event_bus.subscribe(
            [EventType.BLOCKLY_START, EventType.BLOCKLY_STOP],
            self._on_blockly_event
        )
        
        # ★ 新增：订阅 eyeEngine overlay 事件（seq 动画生命周期）
        self.event_bus.subscribe(
            [EventType.SYSTEM_EVENT],
            self._on_overlay_event
        )

        # ★ 新增：订阅 widget_service 事件（定时器、LCD等）
        self.event_bus.subscribe(
            [EventType.WIDGET_EVENT],
            self._on_widget_event
        )

        # ★★★ 新增：订阅小智情绪事件 ★★★
        self.event_bus.subscribe(
            [EventType.XIAOZHI_EMOTION, EventType.XIAOZHI_STATE],
            self._on_xiaozhi_event
        )
        
        # ★★★ 新增：订阅小智指令事件（动作、意图）★★★
        self.event_bus.subscribe(
            [EventType.XIAOZHI_ACTION, EventType.XIAOZHI_INTENT],
            self._on_xiaozhi_command
        )
        
        # ★★★ 新增：订阅 Vision Service 事件 ★★★
        self.event_bus.subscribe(
            [EventType.VISION_EVENT],
            self._on_vision_event
        )
        
        logger.info("[Daemon] 事件订阅已设置")
    
    def _start_zmq_subscribers(self) -> None:
        """启动 ZMQ 订阅器"""
        # 订阅串口语音命令
        self._serial_subscriber = ZMQEventSubscriber(
            endpoint="ipc:///tmp/doly_serial_pub.sock",
            topics=["event.audio.", "voice."],  # 同时订阅旧格式和新格式
            event_bus=self.event_bus,
            source_name="serial"
        )
        
        if self._serial_subscriber.start():
            logger.info("✅ [Daemon] 串口订阅器启动成功")
        else:
            logger.warning("⚠️ [Daemon] 串口订阅器启动失败，继续运行...")
        
        # 订阅传感器事件（drive服务发布到 doly_zmq.sock）
        self._sensor_subscriber = ZMQEventSubscriber(
            endpoint="ipc:///tmp/doly_zmq.sock",
            topics=["sensor.", "io."],
            event_bus=self.event_bus,
            source_name="sensors"
        )
        
        if self._sensor_subscriber.start():
            logger.info("✅ [Daemon] 传感器订阅器启动成功")
        else:
            logger.warning("⚠️ [Daemon] 传感器订阅器启动失败，继续运行...")
        
        # ★ 新增：订阅 eyeEngine 事件（包括 overlay 生命周期事件）
        self._eye_engine_subscriber = ZMQEventSubscriber(
            endpoint="ipc:///tmp/doly_eye_event.sock",
            topics=["overlay."],
            event_bus=self.event_bus,
            source_name="eyeEngine"
        )
        
        if self._eye_engine_subscriber.start():
            logger.info("✅ [Daemon] eyeEngine 事件订阅器启动成功")
        else:
            logger.warning("⚠️ [Daemon] eyeEngine 订阅器启动失败，继续运行...")
        
        # ★★★ 新增：订阅小智 ZMQ 话题 ★★★
        self._xiaozhi_subscriber = ZMQEventSubscriber(
            endpoint="ipc:///tmp/doly_zmq.sock",
            topics=["emotion.xiaozhi", "cmd.xiaozhi."],
            event_bus=self.event_bus,
            source_name="xiaozhi"
        )
        
        if self._xiaozhi_subscriber.start():
            logger.info("✅ [Daemon] 小智订阅器启动成功")
        else:
            logger.warning("⚠️ [Daemon] eyeEngine 事件订阅器启动失败，继续运行...")

        # ★ 新增：订阅 widget_service 事件
        self._widget_subscriber = ZMQEventSubscriber(
            endpoint="ipc:///tmp/doly_widget_pub.sock",
            topics=["event.widget.", "status.widget."],
            event_bus=self.event_bus,
            source_name="widget_service"
        )

        if self._widget_subscriber.start():
            logger.info("✅ [Daemon] widget_service 事件订阅器启动成功")
        else:
            logger.warning("⚠️ [Daemon] widget_service 事件订阅器启动失败，继续运行...")

        # ★★★ 新增：订阅 Vision Service (FaceReco) 事件 ★★★
        vision_endpoint = None
        try:
            vision_endpoint = (
                getattr(self, "face_reco_manager", None)
                and getattr(self.face_reco_manager, "config", None)
                and self.face_reco_manager.config.get("vision_service", {}).get("pub_endpoint")  # ★ 修复：使用 pub_endpoint
            )
        except Exception:
            vision_endpoint = None

        self._vision_subscriber = ZMQEventSubscriber(
            endpoint=vision_endpoint or "ipc:///tmp/doly_vision_events.sock",  # ★ 修复：正确的Vision事件发布端点
            topics=["event.vision.face", "event.vision.face.recognized", "event.vision.capture", "status.vision.state"],
            event_bus=self.event_bus,
            source_name="vision_service"
        )

        if self._vision_subscriber.start():
            logger.info("✅ [Daemon] Vision Service 事件订阅器启动成功")
        else:
            logger.warning("⚠️ [Daemon] Vision Service 事件订阅器启动失败，Vision 功能将不可用")

        # ★★★ 新增：订阅小智状态/情绪事件 ★★★
        self._xiaozhi_subscriber = ZMQEventSubscriber(
            endpoint="ipc:///tmp/doly_xiaozhi_state.sock",
            topics=["status.xiaozhi."],
            event_bus=self.event_bus,
            source_name="xiaozhi"
        )

        if self._xiaozhi_subscriber.start():
            logger.info("✅ [Daemon] 小智状态订阅器启动成功")
        else:
            logger.warning("⚠️ [Daemon] 小智状态订阅器启动失败（小智客户端可能未启动），继续运行...")
    
    def _on_voice_event(self, event: DolyEvent) -> None:
        """处理语音事件"""
        logger.info(f"[Voice] 🎤 收到语音事件: type={event.type.value}, data={event.data}")
        self._event_count += 1
        # 任何语音事件都视为与主人交互，更新最后交互时间
        try:
            cmd = None
            # 标准命令字段
            if isinstance(event.data, dict):
                cmd = event.data.get('command')
            logger.info(f"[Voice] 提取命令: cmd={cmd}")
            # 更新最后交互时间
            self.last_interaction_time = time.time()
            # 如果收到静音命令，切换 actions_muted
            if cmd == 'cmd_SysVolMute':
                self.actions_muted = not getattr(self, 'actions_muted', False)
                logger.info(f"[Voice] 切换静音: actions_muted={self.actions_muted}")
                # 不做后续唤醒流程
                return

            # 退出短睡（如果在短睡中）
            if self._nap_end_time and time.time() < self._nap_end_time:
                logger.info("[Daemon] 收到语音，退出短睡")
                self._nap_end_time = None
                # 我们不把短睡映射为真正的 SLEEPING 状态，因此不需要 state 转换
        except Exception:
            pass
        
        if event.type == EventType.VOICE_WAKEUP:
            logger.info(f"🎤 [Voice] 唤醒事件: {event.data}")
            
            # 🚨 高优先级唤醒逻辑：无条件中断所有动画并转换状态
            logger.info("[Voice] 无条件中断所有动画以处理唤醒")
            if self.animation_integration:
                # 使用同步包装，确保协程被调度执行
                self.animation_integration.interrupt_all_sync()
            
            # 无论当前状态如何，都转换到ACTIVATED（唤醒的核心逻辑）
            current_state = self.state_machine.current_state
            if current_state != DolyState.ACTIVATED:
                logger.info(f"[Voice] 从 {current_state.value} 强制转换到 ACTIVATED")
                self.state_machine.transition_to(DolyState.ACTIVATED)
            else:
                logger.debug("[Voice] 已在ACTIVATED状态，仅重置超时")
            
            self.state_machine.reset_timeout()
            
            # 触发唤醒命令映射（播放 wake_word.xml 动画）
            try:
                command_name = event.data.get('command', 'wakeup_detected')
                logger.debug(f"[Voice] 处理唤醒映射: {command_name}")
                self._process_voice_command(command_name, event.data)
            except Exception as e:
                logger.error(f"[Voice] 处理唤醒映射失败: {e}", exc_info=True)
            
        elif event.type == EventType.VOICE_COMMAND:
            logger.info(f"🎤 [Voice] 命令事件: {event.data}")
            
            # 优先从 command 字段获取，其次尝试从 topic 名称提取
            command_name = event.data.get('command', '')
            
            # 如果没有 command 字段,从 topic 中提取 (例如 event.audio.cmd_ActWhoami -> cmd_ActWhoami)
            if not command_name and event.topic:
                # 从 "event.audio.cmd_ActWhoami" 或其他格式中提取命令
                if event.topic.startswith('event.audio.'):
                    command_name = event.topic.replace('event.audio.', '')
            
            logger.info(f"[Voice] 提取命令: {command_name or '(将通过映射查找)'}")
            
            # ★★★ 新增：命令去重检查（防止音频反馈导致的死循环）★★★
            current_time = time.time()
            if command_name and command_name == self._last_voice_command:
                time_since_last = current_time - self._last_voice_command_time
                if time_since_last < self._voice_command_debounce_seconds:
                    logger.warning(
                        f"[Voice] ⚠️ 命令去重：忽略重复命令 '{command_name}' "
                        f"(距上次 {time_since_last:.1f}s < {self._voice_command_debounce_seconds}s)"
                    )
                    logger.info(f"[Voice] 🛑 去重DEBUG: cmd={command_name}, last={self._last_voice_command}, delta={time_since_last:.2f}s")
                    return
            
            # ★★★ 新增：命令状态锁定检查（防止长时任务被重复触发）★★★
            if command_name and command_name in self._command_locks:
                unlock_time = self._command_locks[command_name]
                if current_time < unlock_time:
                    remaining = unlock_time - current_time
                    logger.warning(
                        f"[Voice] 🔒 命令锁定：'{command_name}' 正在执行中，"
                        f"剩余锁定时间 {remaining:.1f}秒"
                    )
                    return
                else:
                    # 锁定已过期，清除
                    del self._command_locks[command_name]
                    logger.info(f"[Voice] 🔓 命令锁定已解除: {command_name}")
            
            # 记录当前命令
            logger.info(f"[Voice] ✅ 接受命令: {command_name}, 更新去重记录")
            self._last_voice_command = command_name
            self._last_voice_command_time = current_time
            
            # 如果命令需要锁定，设置锁定状态
            if command_name in self._command_lock_config:
                lock_duration = self._command_lock_config[command_name]
                self._command_locks[command_name] = current_time + lock_duration
                logger.info(f"[Voice] 🔒 设置命令锁定: {command_name}, 时长 {lock_duration}秒")
            
            # 如果没有 command 字段,把 topic 也传给 _process_voice_command 用于备用提取
            if not event.data.get('command'):
                event.data['topic'] = event.topic
            
            # 如果没有 command 字段，使用空字符串让 _process_voice_command 自己推导
            self._process_voice_command(command_name, event.data)
    
    def _on_touch_event(self, event: DolyEvent) -> None:
        """处理触摸事件"""
        logger.info(f"👆 [Touch] 触摸事件开始处理")
        
        try:
            # 触摸也视为与主人的交互
            self.last_interaction_time = time.time()
            # 如果正在短睡，收到触摸立即退出短睡（我们不改变主状态，只取消 nap 标志）
            if self._nap_end_time and time.time() < self._nap_end_time:
                logger.info("[Daemon] 收到触摸，退出短睡")
                self._nap_end_time = None

            # 触摸时重置超时计时
            logger.debug("[Touch] 重置超时")
            self.state_machine.reset_timeout()
            
            # 如果在 IDLE 状态，转换为 ACTIVATED
            if self.state_machine.current_state == DolyState.IDLE:
                logger.debug("[Touch] 状态转换到ACTIVATED")
                self.state_machine.transition_to(DolyState.ACTIVATED)
            
            # 解析touch.gesture事件获取详细信息
            has_gesture = event.data.get('gesture') and event.data.get('pin')
            logger.debug(f"[Touch] gesture字段={has_gesture}, topic={event.data.get('topic')}")
            
            # 优先级1: 检查是否有gesture字段（高级touch.gesture事件）
            if has_gesture:
                logger.info(f"[Touch] 识别为高级手势: {event.data.get('gesture')}")
                self._process_touch_gesture_event(event)
            # 优先级2: 检查topic字段
            elif event.data.get('topic') == 'io.pca9535.touch.gesture':
                logger.info(f"[Touch] 通过topic识别为手势事件")
                self._process_touch_gesture_event(event)
            else:
                # 兼容原始pin.change事件
                logger.info(f"[Touch] 处理为pin.change事件")

                # 尝试按照 sensor_event_mapping.yaml 进行更细粒度的处理（参考高级手势处理）
                pin = event.data.get('pin', '')
                value = event.data.get('value', None)

                # 只在按下事件(value True)时触发一次性响应，释放/其它忽略
                if pin and value is True:
                    side = 'left' if pin == 'TOUCH_L' else 'right' if pin == 'TOUCH_R' else None
                    if side:
                        # 以最保守的方式将原始 pin.change 视作单次点击（single_tap）
                        event_name = f"touch_{side}_single_tap"
                        current_state = self.state_machine.current_state.value
                        action = self.sensor_config.get_action(event_name, current_state)
                        if action:
                            logger.info(f"[Touch] 使用 sensor mapping 处理: {event_name} -> {action.get('action', {})}")
                            # 检查冷却
                            cooldown = action.get('cooldown', 1.0)
                            if self.event_throttler.is_throttled(event_name, cooldown):
                                logger.debug(f"[Touch] 事件在冷却中: {event_name}")
                            else:
                                self._execute_sensor_action(event_name, action, event.data)
                            # 已处理，直接返回
                            return

                # 回退到基于 CommandMapper 的通用映射（兼容历史配置）
                self._process_mapped_event_candidates([
                    event.data.get('topic', ''),
                    event.type.value
                ], event.data)
        except Exception as e:
            logger.error(f"[Touch] 处理失败: {e}", exc_info=True)
        finally:
            logger.info(f"👆 [Touch] 处理完成")
    
    def _process_touch_gesture_event(self, event: DolyEvent) -> None:
        """
        处理高级触摸手势事件
        
        Args:
            event: 来自drive的touch.gesture事件
        """
        try:
            # 提取手势信息
            pin = event.data.get('pin', '')  # TOUCH_L / TOUCH_R
            gesture = event.data.get('gesture', '')  # SINGLE / DOUBLE / LONG_PRESS
            
            if not pin or not gesture:
                logger.warning(f"[Touch] 缺少手势信息: pin={pin}, gesture={gesture}")
                return
            
            # 识别多次按压（如LONG_PRESS_X2, LONG_PRESS_X3）
            recognized_gesture = self.gesture_recognizer.recognize_multipress(pin, gesture)
            logger.info(f"[Touch] 识别手势: {gesture} -> {recognized_gesture}")
            
            # 构造事件名称
            side = 'left' if pin == 'TOUCH_L' else 'right'
            event_name = f"touch_{side}_{recognized_gesture.lower()}"
            
            # 从配置获取当前state相关的动作
            current_state = self.state_machine.current_state.value
            action = self.sensor_config.get_action(event_name, current_state)
            
            if not action:
                logger.debug(f"[Touch] 未找到 {event_name} 在 {current_state} 的配置")
                return
            
            # 检查冷却时间
            cooldown = action.get('cooldown', 1.0)
            if self.event_throttler.is_throttled(event_name, cooldown):
                logger.debug(f"[Touch] 事件在冷却中: {event_name}")
                return
            
            logger.info(f"[Touch] 执行动作: {event_name} -> {action.get('action', {})}")
            
            # 执行动作
            self._execute_sensor_action(event_name, action, event.data)
            
        except Exception as e:
            logger.error(f"[Touch] 处理手势异常: {e}", exc_info=True)
    
    def _on_sensor_event(self, event: DolyEvent) -> None:
        """处理传感器事件"""
        logger.info(f"📡 [Sensor] 传感器事件: {event.type.value}, data={event.data}")
        
        # 如果在探索模式，转发事件给exploration_manager
        if (self.state_machine.current_state == DolyState.EXPLORING and
            self.exploration_manager and self.exploration_manager.running):
            try:
                try:
                    asyncio.create_task(self.exploration_manager._handle_sensor_event(event))
                except RuntimeError:
                    # 如果没有 event loop，在主 loop 中创建任务
                    self.loop.create_task(self.exploration_manager._handle_sensor_event(event))
            except Exception as e:
                logger.error(f"[Sensor] 转发事件给exploration_manager失败: {e}")
        
        # 悬崖事件处理（最高优先级）
        if event.type == EventType.CLIFF_DETECTED:
            logger.debug(f"[Sensor] 识别为高级悬崖事件，调用_process_cliff_detected_event")
            self._process_cliff_detected_event(event)
        else:
            logger.debug(f"[Sensor] 识别为其他传感器事件，使用配置化映射")
            # 其他传感器事件的配置化映射
            self._process_mapped_event_candidates([
                event.data.get('topic', ''),
                event.type.value
            ], event.data)
    
    def _process_cliff_detected_event(self, event: DolyEvent) -> None:
        """
        处理悬崖检测事件（最高优先级，中断当前操作）
        
        Args:
            event: 来自drive的cliff.pattern事件或pin.change事件
        """
        try:
            # 支持两种数据格式：
            # 1. 高级格式（cliff.pattern）：包含 pattern 和 position
            # 2. 低级格式（pin.change）：仅包含 pin 和 value
            
            pattern = event.data.get('pattern', '')  # 高级格式：CLIFF / STABLE / NOISY
            position = event.data.get('position', '')  # 高级格式：fl / fr / bl / br
            pin = event.data.get('pin', '')  # 低级格式：IRS_FL / IRS_FR / IRS_BL / IRS_BR
            value = event.data.get('value', None)  # 低级格式：True/False（低电平表示检测到）

            # 统一 normalize position 名称，以匹配 sensor_event_mapping.yaml 中的 key
            position_norm_map = {
                'fl': 'front_left',
                'fr': 'front_right',
                'bl': 'back_left',
                'br': 'back_right'
            }
            if position in position_norm_map:
                position = position_norm_map[position]
            
            # 如果是低级pin.change格式，则转换为高级格式
            if not pattern and pin:
                # value=False (低电平) = 检测到悬崖
                if value is False:
                    pattern = 'CLIFF'
                    # 从pin名称提取位置
                    position_map = {
                        'IRS_FL': 'front_left',
                        'IRS_FR': 'front_right', 
                        'IRS_BL': 'back_left',
                        'IRS_BR': 'back_right'
                    }
                    position = position_map.get(pin, position)
                else:
                    # value=True (高电平) = 未检测到，忽略
                    logger.debug(f"[Cliff] pin为高电平，未检测到悬崖: {pin}")
                    return
            
            # 只在真实检测到悬崖时响应
            # if pattern != 'CLIFF':
            #     logger.debug(f"[Cliff] 非悬崖状态，忽略: pattern={pattern}")
            #     return
            
            # if not position:
            #     logger.warning(f"[Cliff] 缺少位置信息: {event.data}")
            #     return
            
            # logger.warning(f"⚠️ [Cliff] 检测到悬崖: {position}")
            
            # 构造事件名称
            event_name = f"cliff_{position}"
            
            # 悬崖事件在ANY状态下都要响应（最高优先级）
            action = self.sensor_config.get_action(event_name, 'ANY')
            
            if not action:
                logger.warning(f"[Cliff] 未找到 {event_name} 的配置，执行默认避让")
                # 执行默认避让动作
                action = {'action': {'animation': 'edge_fb.xml'}}
            
            # 检查冷却时间（悬崖事件也需要冷却以防止抖动导致连续触发）
            cooldown = action.get('cooldown', 3.0)
            if self.event_throttler.is_throttled(event_name, cooldown):
                logger.debug(f"[Cliff] 事件在冷却中: {event_name}")
                return
            
            # 中断当前所有动画/操作
            if self.animation_integration:
                try:
                    logger.info("[Cliff] 中断当前动画")
                    # 注意：interrupt_all()需要在animation_integration中实现
                    if hasattr(self.animation_integration, 'interrupt_all'):
                        self.animation_integration.interrupt_all_sync()
                except Exception as e:
                    logger.error(f"[Cliff] 中断动画失败: {e}")
            
            # 执行避让动作
            logger.info(f"[Cliff] 执行避让: {event_name} -> {action.get('action', {})}")
            self._execute_sensor_action(event_name, action, event.data)
            
        except Exception as e:
            logger.error(f"[Cliff] 处理悬崖事件异常: {e}", exc_info=True)
    
    def _setup_blockly_runtime(self) -> None:
        """
        ★ 新增：设置 Blockly C++ 运行时
        
        初始化 BlocklyRuntimeBinding，准备执行 Blockly 程序
        """
        try:
            self.blockly_runtime = BlocklyRuntimeBinding()
            
            if not self.blockly_runtime.initialize():
                logger.warning("⚠️ [Daemon] Blockly 运行时初始化失败，将使用 Python 实现")
            else:
                logger.info("✅ [Daemon] Blockly 运行时初始化成功")
                
        except Exception as e:
            logger.error(f"[Daemon] Blockly 运行时设置失败: {e}")
            self.blockly_runtime = None
    
    def _setup_exploration_manager(self) -> None:
        """
        ★ 新增：设置探索模式管理器
        
        初始化 ExplorationManager，准备自主巡航行为
        """
        try:
            # 需要获取 drive_interface
            drive_interface = None
            if self.animation_integration and hasattr(self.animation_integration, 'interfaces') and self.animation_integration.interfaces:
                drive_interface = self.animation_integration.interfaces.drive
            
            if not drive_interface:
                logger.warning("⚠️ [Daemon] drive_interface 未初始化，探索模式可能无法正常运行")
            
            self.exploration_manager = ExplorationManager(
                config_path=str(self.config_dir / "exploration.yaml"),
                drive_interface=drive_interface,
                event_bus=self.event_bus
            )
            
            logger.info("✅ [Daemon] 探索模式管理器初始化成功")
            
        except Exception as e:
            logger.error(f"[Daemon] 探索管理器设置失败: {e}")
            import traceback
            traceback.print_exc()
            self.exploration_manager = None
    
    def _setup_gesture_interaction_manager(self) -> None:
        """
        ★ 新增：设置手势互动管理器
        
        初始化 GestureInteractionManager，处理TOF手势互动
        """
        try:
            self.gesture_interaction_manager = GestureInteractionManager(
                daemon=self,
                config_path=str(self.config_dir / "gesture_interaction.yaml")
            )
            
            logger.info("✅ [Daemon] 手势互动管理器初始化成功")
            
        except Exception as e:
            logger.error(f"[Daemon] 手势互动管理器设置失败: {e}")
            import traceback
            traceback.print_exc()
            self.gesture_interaction_manager = None
    
    def _on_blockly_event(self, event: DolyEvent) -> None:
        """处理 Blockly 事件"""
        if event.type == EventType.BLOCKLY_START:
            logger.info("🧩 [Blockly] 开始执行 Blockly 代码")
            self.state_machine.switch_mode(DolyMode.BLOCKLY)
            
        elif event.type == EventType.BLOCKLY_STOP:
            logger.info("🧩 [Blockly] Blockly 执行完成")
            self.state_machine.switch_mode(DolyMode.AUTONOMOUS)
    
    def handle_blockly_program(self, xml_code: str) -> bool:
        """
        ★ 新增：处理接收到的 Blockly 程序
        
        此方法由 WebSocket 服务调用，用于启动 Blockly 程序执行
        
        Args:
            xml_code: Blockly XML 代码
        
        Returns:
            True 如果程序成功启动
        """
        try:
            if not self.blockly_runtime:
                logger.error("[Blockly] Blockly 运行时未初始化")
                return False
            
            logger.info("[Blockly] 收到程序，开始解析")
            
            # 1. 解析 XML
            program = self.blockly_runtime.parse_xml(xml_code)
            if not program:
                logger.error("[Blockly] XML 解析失败")
                return False
            
            logger.info(f"[Blockly] 程序解析成功: ID={program.get('program_id')}")
            
            # 2. 启动程序
            if not self.blockly_runtime.start_program(program):
                logger.error("[Blockly] 程序启动失败")
                return False
            
            # 3. 转换状态
            self.state_machine.switch_mode(DolyMode.BLOCKLY)
            
            logger.info("[Blockly] ✅ 程序已启动执行")
            return True
            
        except Exception as e:
            logger.error(f"[Blockly] 处理程序失败: {e}", exc_info=True)
            return False
    
    def stop_blockly_program(self) -> bool:
        """
        ★ 新增：停止当前运行的 Blockly 程序
        
        Returns:
            True 如果程序停止成功
        """
        try:
            if not self.blockly_runtime:
                logger.warning("[Blockly] Blockly 运行时未初始化")
                return False
            
            logger.info("[Blockly] 停止 Blockly 程序")
            
            # 调用 runtime 的 stop_program 方法
            result = self.blockly_runtime.stop_program()
            
            # 转换回自主模式
            self.state_machine.switch_mode(DolyMode.AUTONOMOUS)
            
            if result:
                logger.info("[Blockly] ✅ 程序已停止")
            else:
                logger.warning("[Blockly] 程序停止可能不完整")
            
            return result
            
        except Exception as e:
            logger.error(f"[Blockly] 停止程序失败: {e}", exc_info=True)
            return False
    
    def get_blockly_status(self) -> dict:
        """
        ★ 新增：获取当前 Blockly 程序的执行状态
        
        Returns:
            状态字典
        """
        try:
            if not self.blockly_runtime:
                return {
                    "program_id": None,
                    "is_running": False,
                    "state": "not_initialized"
                }
            
            state = self.blockly_runtime.get_state()
            if state:
                return state
            else:
                return {
                    "program_id": None,
                    "is_running": False,
                    "state": "idle"
                }
        
        except Exception as e:
            logger.error(f"[Blockly] 获取状态失败: {e}")
            return {
                "program_id": None,
                "is_running": False,
                "state": "error",
                "error": str(e)
            }
    
    def _on_overlay_event(self, event: DolyEvent) -> None:
        """
        ★ 新增：处理 eyeEngine 发布的 overlay 事件（seq 动画生命周期）
        """
        if not event.data:
            return
        
        topic = event.data.get('topic', '')
        overlay_id = event.data.get('overlay_id')
        
        if not overlay_id:
            return
        
        try:
            if topic == 'overlay.started':
                # seq 动画已在 eyeEngine 开始播放，确认并追踪
                # 注意：这在 ZMQEventSubscriber 线程中调用，需要用 asyncio.run_coroutine_threadsafe
                import asyncio
                if self.loop and self.loop.is_running():
                    asyncio.run_coroutine_threadsafe(
                        self.animation_integration.register_confirmed_overlay(overlay_id),
                        self.loop
                    )
                    logger.info(f"[SeqTracker] 接收 overlay.started 事件: {overlay_id}")
                    # ★ 如果我们正在等待 snooze 动画开始，记录其 overlay_id
                    if self._snooze_in_progress and not self._snooze_overlay_id:
                        logger.info(f"[SeqTracker] 捕获 snooze 动画的 overlay_id: {overlay_id}")
                        self._snooze_overlay_id = overlay_id
                else:
                    logger.warning(f"[SeqTracker] daemon event loop 未运行，无法注册 overlay: {overlay_id}")
            
            elif topic == 'overlay.completed':
                # seq 动画自然完成
                import asyncio
                if self.loop and self.loop.is_running():
                    asyncio.run_coroutine_threadsafe(
                        self.animation_integration.unregister_overlay(overlay_id),
                        self.loop
                    )
                    logger.info(f"[SeqTracker] 接收 overlay.completed 事件: {overlay_id}")
                    # ★ 如果这是 snooze 动画完成，重置 snooze 状态允许下一次触发
                    if self._snooze_in_progress and self._snooze_overlay_id == overlay_id:
                        logger.info(f"[SeqTracker] snooze 动画完成，重置 snooze 状态")
                        self._snooze_in_progress = False
                        self._snooze_overlay_id = None
                        # 重置交互时间，这样需要再等 snooze_after 秒后才能再次触发 snooze
                        self.last_interaction_time = time.time()
                else:
                    logger.warning(f"[SeqTracker] daemon event loop 未运行，无法注销 overlay: {overlay_id}")
            
            elif topic == 'overlay.stopped':
                # seq 动画被停止命令中断
                import asyncio
                if self.loop and self.loop.is_running():
                    asyncio.run_coroutine_threadsafe(
                        self.animation_integration.unregister_overlay(overlay_id),
                        self.loop
                    )
                    logger.info(f"[SeqTracker] 接收 overlay.stopped 事件: {overlay_id}")
                    # ★ 如果这是 snooze 动画被中断，重置 snooze 状态
                    if self._snooze_in_progress and self._snooze_overlay_id == overlay_id:
                        logger.info(f"[SeqTracker] snooze 动画被中断，重置 snooze 状态")
                        self._snooze_in_progress = False
                        self._snooze_overlay_id = None
                else:
                    logger.warning(f"[SeqTracker] daemon event loop 未运行，无法注销 overlay: {overlay_id}")
            
            elif topic == 'overlay.failed':
                # seq 动画播放失败
                import asyncio
                if self.loop and self.loop.is_running():
                    asyncio.run_coroutine_threadsafe(
                        self.animation_integration.unregister_overlay(overlay_id),
                        self.loop
                    )
                    logger.warning(f"[SeqTracker] 接收 overlay.failed 事件: {overlay_id}")
                    # ★ 如果这是 snooze 动画失败，重置 snooze 状态
                    if self._snooze_in_progress and self._snooze_overlay_id == overlay_id:
                        logger.warning(f"[SeqTracker] snooze 动画播放失败，重置 snooze 状态")
                        self._snooze_in_progress = False
                        self._snooze_overlay_id = None
                else:
                    logger.warning(f"[SeqTracker] daemon event loop 未运行，无法注销 overlay: {overlay_id}")
        
        except Exception as e:
            logger.error(f"[SeqTracker] overlay 事件处理异常: {e}")

    def _on_widget_event(self, event: DolyEvent) -> None:
        """处理 widget_service 发布的事件（计时器、时钟、LCD）"""
        if not event.data:
            return

        topic = event.data.get('topic', '')
        if not topic:
            return

        # 定时器事件 -> 播放动画映射
        timer_map = {
            'event.widget.timer.started': 'timer_start',
            'event.widget.timer.paused': 'timer_pause',
            'event.widget.timer.resumed': 'timer_resume',
            'event.widget.timer.stopped': 'timer_stop',
            'event.widget.timer.finished': 'countdown_complete',
            # ★★★ 修复：添加实际的完成事件主题 ★★★
            'event.widget.timer.event.eye.timer.finished': 'countdown_complete',
        }

        try:
            logger.debug(f"🔔 [WidgetEvent] 收到事件: topic={topic}")
            
            if topic in timer_map:
                self.timer_manager.handle_timer_event(timer_map[topic], event.data)
                
                # ★★★ 修复：定时器完成后自动隐藏 widget ★★★
                # 匹配两种可能的完成事件
                if topic in ['event.widget.timer.finished', 'event.widget.timer.event.eye.timer.finished']:
                    logger.info("🎯 [WidgetEvent] 倒计时完成，触发自动隐藏")
                    # 延迟一小段时间后隐藏（给用户看到完成状态）
                    def hide_after_delay():
                        time.sleep(2.0)  # 延迟2秒让用户看到完成状态
                        try:
                            self.widget_client.hide_widget()
                            logger.info("✅ [WidgetEvent] 已自动隐藏 widget")
                        except Exception as e:
                            logger.error(f"❌ [WidgetEvent] 隐藏 widget 失败: {e}")
                    
                    threading.Thread(target=hide_after_delay, daemon=True).start()
                return

            if topic == 'event.widget.clock.chime':
                self.timer_manager.handle_timer_event('hourly_chime', event.data)
                return

            # 其他事件（lcd_acquired/released）暂不处理，这部分由 eyeEngine 自身监听
        except Exception as e:
            logger.error(f"[WidgetEvent] 处理失败 topic={topic}: {e}")

    def _on_xiaozhi_event(self, event: DolyEvent) -> None:
        """
        处理小智状态/情绪事件
        
        Args:
            event: 小智事件
        """
        if not event.data:
            return

        try:
            topic = event.data.get('topic', '')
            
            # 情绪事件
            if event.type == EventType.XIAOZHI_EMOTION or topic == 'status.xiaozhi.emotion':
                emotion = event.data.get('emotion', '')
                timestamp_ms = event.data.get('timestamp_ms', int(time.time() * 1000))
                
                logger.info(f"🎭 [Xiaozhi] 收到情绪事件: emotion={emotion}")
                
                # 调用情绪管理器处理
                if hasattr(self, 'xiaozhi_emotion_manager') and self.xiaozhi_emotion_manager:
                    self.xiaozhi_emotion_manager.handle_emotion_event({
                        'emotion': emotion,
                        'timestamp_ms': timestamp_ms
                    })
                else:
                    logger.warning("[Xiaozhi] 情绪管理器未初始化")
                return
            
            # 状态事件
            if event.type == EventType.XIAOZHI_STATE or topic == 'status.xiaozhi.state':
                state = event.data.get('state', -1)
                timestamp_ms = event.data.get('timestamp_ms', int(time.time() * 1000))
                
                logger.debug(f"[Xiaozhi] 收到状态事件: state={state}")
                # 状态事件目前仅记录日志，暂不做特殊处理
                # 可用于跟踪小智是否在说话/聆听等
                return

        except Exception as e:
            logger.error(f"[Xiaozhi] 处理事件失败: {e}", exc_info=True)

    def _on_xiaozhi_command(self, event: DolyEvent) -> None:
        """
        处理小智动作和意图指令
        
        Args:
            event: 小智指令事件
        """
        try:
            # 动作指令
            if event.type == EventType.XIAOZHI_ACTION:
                self.xiaozhi_manager.handle_action(event)
                return
            
            # 意图指令
            if event.type == EventType.XIAOZHI_INTENT:
                self.xiaozhi_manager.handle_intent(event)
                return

        except Exception as e:
            logger.error(f"[Xiaozhi] 处理指令失败: {e}", exc_info=True)
    
    # ==================== Vision Service 事件处理 ====================
    
    def _on_vision_event(self, event: DolyEvent) -> None:
        """
        处理 Vision Service (FaceReco) 事件
        
        Args:
            event: Vision 事件
        """
        try:
            # logger.info(f"[Vision] 📨 接收到 Vision 事件: topic={event.topic}, data={event.data}")
            
            # 将事件转发给 FaceRecoManager
            if hasattr(self, 'face_reco_manager') and self.face_reco_manager:
                # logger.info(f"[Vision] ✅ 转发事件给 FaceRecoManager: {event.topic}")
                self.face_reco_manager.handle_event(event)
            else:
                logger.warning("[Vision] ❌ FaceRecoManager 未初始化")
        except Exception as e:
            logger.error(f"[Vision] 处理 Vision 事件失败: {e}", exc_info=True)

    # ==================== FaceReco 状态机联动 ====================

    def _handle_face_recognized_state(self, face) -> None:
        """人脸识别成功回调：进入 face_recognition 状态"""
        try:
            current = self.state_machine.current_state
            if current != DolyState.FACE_RECOGNITION:
                logger.info(f"[State] 人脸识别触发状态切换: {current.value} -> face_recognition")
                self.state_machine.transition_to(DolyState.FACE_RECOGNITION)
            self.state_machine.reset_timeout()
            self.last_interaction_time = time.time()
        except Exception as e:
            logger.error(f"[State] 人脸识别状态联动失败: {e}")

    def _handle_face_new_state(self, face) -> None:
        """新人脸出现：若处于 idle，提升到 activated"""
        try:
            if self.state_machine.current_state == DolyState.IDLE:
                logger.info("[State] 新人脸触发激活状态")
                self.state_machine.transition_to(DolyState.ACTIVATED)
                self.state_machine.reset_timeout()
                self.last_interaction_time = time.time()
        except Exception as e:
            logger.error(f"[State] 新人脸状态联动失败: {e}")

    def _handle_all_faces_lost_state(self) -> None:
        """全部人脸消失：从 face_recognition 回到 idle"""
        try:
            if self.state_machine.current_state == DolyState.FACE_RECOGNITION:
                logger.info("[State] 人脸消失，返回 idle 状态")
                self.state_machine.transition_to(DolyState.IDLE)
        except Exception as e:
            logger.error(f"[State] 人脸消失状态联动失败: {e}")
    
    # ==================== 小智回调处理器 ====================
    
    def _handle_xiaozhi_emotion(self, emotion: str, intensity: int) -> None:
        """
        处理小智情绪回调
        
        Args:
            emotion: 情绪名称
            intensity: 情绪强度
        """
        logger.info(f"💗 [Daemon] 小智情绪: {emotion} (intensity={intensity})")
        
        # 调用情绪管理器处理
        if hasattr(self, 'xiaozhi_emotion_manager') and self.xiaozhi_emotion_manager:
            self.xiaozhi_emotion_manager.handle_emotion_event({
                'emotion': emotion,
                'intensity': intensity
            })
    
    def _handle_xiaozhi_action(self, action_type: str, params: Dict[str, Any], priority: int) -> None:
        """
        处理小智动作回调
        
        Args:
            action_type: 动作类型
            params: 动作参数
            priority: 优先级
        """
        logger.info(f"🎬 [Daemon] 小智动作: {action_type} (priority={priority})")
        
        # 使用 TaskEngine 处理动作
        asyncio.run_coroutine_threadsafe(
            self.task_engine.process_action(action_type, params),
            self.loop
        )
    
    def _handle_xiaozhi_intent(self, intent: str, entities: Dict[str, Any], text: str) -> None:
        """
        处理小智意图回调
        
        Args:
            intent: 意图名称
            entities: 实体参数
            text: 原始文本
        """
        logger.info(f"💡 [Daemon] 小智意图: {intent}")
        
        # 使用 TaskEngine 处理意图
        asyncio.run_coroutine_threadsafe(
            self.task_engine.process_intent(intent, entities),
            self.loop
        )
    
    # ==================== 系统错误处理 ====================
    
    def _on_system_error(self, event: DolyEvent) -> None:
        """
        处理系统错误事件（未知事件类型、异常等）
        
        Args:
            event: 系统错误事件
        """
        if not event.data:
            return

        try:
            # 系统错误事件通常由以下场景生成：
            # 1. ZMQ 收到未知的事件类型
            # 2. 事件反序列化失败
            # 3. 其他系统异常
            
            error_type = event.data.get('error_type', 'unknown')
            error_msg = event.data.get('message', '未知错误')
            source = event.data.get('source', event.source or 'unknown')
            
            logger.warning(f"⚠️ [SystemError] {error_type} from {source}: {error_msg}")
            
            # 可以在这里添加更多的错误处理逻辑
            # 例如：发送告警、重试、记录到文件等
            
        except Exception as e:
            logger.error(f"[SystemError] 事件处理异常: {e}", exc_info=True)
    
    def _on_state_changed(self, old_state: DolyState, new_state: DolyState) -> None:
        """状态变化回调"""
        logger.info(f"🔄 [State] 状态变化: {old_state.value} -> {new_state.value}")
        
        # ★★★ 应用新状态的 LED 配置 ★★★
        try:
            self.state_behavior_manager.apply_led_for_state(new_state.value)
        except Exception as e:
            logger.error(f"[State] 应用 LED 配置失败: {e}")
        
        # ★★★ 重置手势历史 ★★★
        try:
            self.sensor_manager.reset_gesture_history()
        except Exception as e:
            logger.error(f"[State] 重置手势历史失败: {e}")
        
        # 可添加状态变化时的动画或音效
        if new_state == DolyState.ACTIVATED:
            logger.debug("[State] 进入激活状态")
        elif new_state == DolyState.FACE_RECOGNITION:
            logger.debug("[State] 进入人脸识别状态")
        elif new_state == DolyState.EXPLORING:
            logger.debug("[State] 进入探索状态")
            # ★ 新增：启动探索管理器
            try:
                logger.info(f"[State] 检查探索管理器: exploration_manager={self.exploration_manager is not None}")
                # 如果探索管理器还没初始化或初始化失败，尝试重新初始化
                if not self.exploration_manager:
                    logger.info("[State] 探索管理器未初始化，尝试初始化...")
                    self._setup_exploration_manager()
                    logger.info(f"[State] 初始化后: exploration_manager={self.exploration_manager is not None}")
                
                if self.exploration_manager and not self.exploration_manager.running:
                    # 在后台任务中启动探索
                    try:
                        # 首先尝试在当前 event loop 中创建任务
                        task = asyncio.create_task(self.exploration_manager.start())
                    except RuntimeError:
                        # 如果没有 event loop，在主 loop 中创建任务
                        task = self.loop.create_task(self.exploration_manager.start())
                    logger.info("[State] 启动探索模式管理器")
                else:
                    logger.warning("[State] 探索管理器未初始化或已在运行")
            except Exception as e:
                logger.warning(f"[State] 启动探索管理器失败: {e}")
        elif new_state == DolyState.SLEEPING:
            logger.debug("[State] 进入休眠状态")
            # ★ 新增：停止探索管理器
            try:
                if self.exploration_manager and self.exploration_manager.running:
                    try:
                        # 尝试在当前 event loop 中创建任务
                        asyncio.create_task(self.exploration_manager.stop())
                    except RuntimeError:
                        # 如果没有 event loop，在主 loop 中创建任务
                        self.loop.create_task(self.exploration_manager.stop())
                    logger.info("[State] 停止探索模式管理器")
            except Exception as e:
                logger.warning(f"[State] 停止探索管理器失败: {e}")
            
            # 尝试根据配置或 state machine 的 entry_animation 播放进入休眠动画
            try:
                # 优先使用 state_behaviors 的 entry_action
                sb = self.state_behaviors.get('sleep') if isinstance(self.state_behaviors, dict) else None
                played = False
                if sb and isinstance(sb, dict):
                    entry = sb.get('entry_action')
                    if entry and entry.get('type') == 'animation' and self.animation_integration:
                        category = entry.get('category')
                        level = entry.get('level', 1)
                        if category in self.animation_integration.get_categories():
                            coro = self.animation_integration.play_animation_by_category(category, level, random_select=True)
                            if asyncio.iscoroutine(coro):
                                self.loop.create_task(coro)
                            played = True
                # 否则使用 state_machine 中的 entry_animation 文件名
                if not played:
                    cfg = self.state_machine.DEFAULT_STATE_CONFIGS.get(DolyState.SLEEPING)
                    if cfg and cfg.entry_animation and self.animation_integration:
                        # 尝试按文件名播放
                        coro = self.animation_integration.play_animation_by_file(cfg.entry_animation)
                        if asyncio.iscoroutine(coro):
                            self.loop.create_task(coro)
            except Exception as e:
                logger.warning(f"[State] 触发休眠动画失败: {e}")
        # 如果目标状态在配置中被禁用，尝试回退到旧状态（或上一个允许的状态）
        try:
            if not self._suppress_state_change_revert:
                try:
                    state_name = new_state.value
                except Exception:
                    state_name = str(new_state)
                sb = self.state_behaviors.get(state_name) if isinstance(self.state_behaviors, dict) else None
                if sb and isinstance(sb, dict) and sb.get('enabled') is False:
                    logger.info(f"[State] 目标状态 {state_name} 在配置中被禁用，尝试回退到 {old_state.value}")
                    # 为避免递归回退循环，设置标志
                    self._suppress_state_change_revert = True
                    try:
                        # 强制回退到旧状态（忽略 allow_interrupt）
                        self.state_machine.transition_to(old_state, force=True)
                    except Exception as e:
                        logger.error(f"[State] 回退到 {old_state.value} 失败: {e}")
                    finally:
                        self._suppress_state_change_revert = False
                    # 不再继续后续的 entry actions / LED 应用
                    return
        except Exception:
            pass

        # 每次状态变更时，尝试应用该状态的 LED 配置（如果存在）
        try:
            try:
                state_name = new_state.value
            except Exception:
                state_name = str(new_state)
            # 当进入 IDLE 时，将 last_interaction_time 重置为当前时间，避免在刚进入时被判定为无交互而立即触发 snooze
            try:
                if new_state == DolyState.IDLE:
                    self.last_interaction_time = time.time()
            except Exception:
                pass
            self._apply_led_for_state(state_name)
        except Exception as e:
            logger.debug(f"[State] 应用 LED 配置失败: {e}")

    def reload_state_behaviors(self) -> None:
        """重新加载 state_behaviors.yaml（可用于 SIGHUP 热加载）。"""
        try:
            import yaml
            cfg_path = self.config_dir / "state_behaviors.yaml"
            if cfg_path.exists():
                with open(cfg_path, 'r', encoding='utf-8') as f:
                    self.state_behaviors = yaml.safe_load(f) or {}
                # 重新解析 sleep schedules
                self._sleep_schedules = []
                sleep_cfg = self.state_behaviors.get('sleep') if isinstance(self.state_behaviors, dict) else None
                if sleep_cfg and isinstance(sleep_cfg, dict):
                    for seg in sleep_cfg.get('schedule', []):
                        start = seg.get('start')
                        end = seg.get('end')
                        if start and end:
                            self._sleep_schedules.append((start, end))
                logger.info(f"✅ [Daemon] 热加载完成: {cfg_path} 睡眠时段={self._sleep_schedules}")
                # 热加载完成后立即应用当前状态的 LED
                try:
                    cur = self.state_machine.current_state.value
                    self._apply_led_for_state(cur)
                except Exception:
                    pass
            else:
                logger.info(f"[Daemon] 热加载配置未找到: {cfg_path}")
        except Exception as e:
            logger.error(f"[Daemon] 重新加载状态行为配置失败: {e}")

    def _apply_led_for_state(self, state_name: str) -> None:
        """
        根据 state_name 在 self.state_behaviors 中查找 led 配置并应用。
        支持的 effect: solid | breathe | blink | gradient | off
        实现方式：优先使用 drive 端的 direct_command（led_rgb / led_ws / led_off），
        如果底层提供了 animation_system 的 LED 接口也可以异步调用。
        """
        try:
            if not isinstance(self.state_behaviors, dict):
                return
            sb = self.state_behaviors.get(state_name) or self.state_behaviors.get(state_name.lower())
            if not sb:
                return
            led_cfg = sb.get('led')
            if not led_cfg:
                return

            effect = led_cfg.get('effect', 'solid')
            color = led_cfg.get('color', '#FFFFFF')
            side = led_cfg.get('side', 'both')
            speed = int(led_cfg.get('speed', 50))

            # map side to numeric for drive API: both->0, left->1, right->2
            side_map = {'both': 0, 'left': 1, 'right': 2}
            side_num = side_map.get(str(side).lower(), 0)

            # Try to use animation_system LED interface if available (async)
            if self.animation_integration and hasattr(self.animation_integration, 'led_interface'):
                led_iface = getattr(self.animation_integration, 'led_interface')
                try:
                    import asyncio
                    if effect == 'breathe':
                        # duration derived from speed (smaller speed => slower breath)
                        duration_ms = max(5000, 20000 - (speed * 150))
                        coro = led_iface.set_color_with_fade(color, duration_ms, side=side_num)
                        if asyncio.iscoroutine(coro):
                            asyncio.run_coroutine_threadsafe(coro, self.loop)
                        return
                    elif effect == 'solid':
                        coro = led_iface.set_color(color, side=side_num)
                        if asyncio.iscoroutine(coro):
                            asyncio.run_coroutine_threadsafe(coro, self.loop)
                        return
                    elif effect == 'off':
                        coro = led_iface.turn_off(side=side_num)
                        if asyncio.iscoroutine(coro):
                            asyncio.run_coroutine_threadsafe(coro, self.loop)
                        return
                    elif effect == 'gradient' or effect == 'rainbow':
                        # fallback to drive rainbow command
                        pass
                    elif effect == 'blink':
                        # use drive led_ws or led_rgb blink style
                        pass
                except Exception:
                    # fallback to drive commands below
                    pass

            # Fallback: send direct_command to drive (led_rgb / led_ws / led_off)
            if effect == 'off':
                self._execute_direct_command('drive', 'led_off', {'side': side})
            elif effect == 'solid':
                # map color '#rrggbb' to hex string expected by drive led_rgb
                self._execute_direct_command('drive', 'led_rgb', {'mode': 'solid', 'color': color, 'side': side})
            elif effect == 'breathe':
                self._execute_direct_command('drive', 'led_rgb', {'mode': 'breath', 'color': color, 'speed': speed, 'side': side})
            elif effect == 'gradient' or effect == 'rainbow':
                self._execute_direct_command('drive', 'led_rgb', {'mode': 'rainbow', 'side': side})
            elif effect == 'blink':
                self._execute_direct_command('drive', 'led_ws', {'style': 'blink', 'color': color, 'speed': speed, 'side': side})
            else:
                # unknown effect, set solid as fallback
                self._execute_direct_command('drive', 'led_rgb', {'mode': 'solid', 'color': color, 'side': side})

        except Exception as e:
            logger.debug(f"[LED] 应用 LED 配置异常: {e}")
    
    def _process_voice_command(self, command_name: str, data: Dict[str, Any]) -> None:
        """
        处理语音命令 - 委托给 VoiceCommandManager
        
        Args:
            command_name: 命令名称
            data: 命令数据
        """
        self._command_count += 1
        
        # 尝试多种方式获取命令名
        if not command_name:
            # 方法1: 从 data 中的 topic 字段提取 (来自真实serial_service: event.audio.*)
            if 'topic' in data:
                topic = data['topic']
                command_name = topic.split('.')[-1]  # 从 "event.audio.cmd_ActWhoami" 提取 "cmd_ActWhoami"
            
            # 方法2: 从 code/hex 查找映射（串口命令）
            elif 'code' in data or 'hex' in data:
                # 使用 serial.yaml 中的映射
                code = data.get('code')
                hex_val = data.get('hex', f"0x{code:02x}" if code is not None else None)
                
                if code is not None or hex_val:
                    try:
                        # 加载 serial.yaml 的映射
                        if not hasattr(self, '_serial_code_map'):
                            self._serial_code_map = {}
                            import yaml
                            config_path = self.config_dir / "serial.yaml"
                            if config_path.exists():
                                with open(config_path, 'r', encoding='utf-8') as f:
                                    serial_config = yaml.safe_load(f) or {}
                                    mappings = serial_config.get('mappings', {})
                                    for key, topic in mappings.items():
                                        # key 可能是 "0x07"（字符串）或 7（整数，YAML 解析的十六进制）
                                        cmd = topic.split('.')[-1]
                                        # 存储多种格式以支持各种查询方式
                                        if isinstance(key, str):
                                            self._serial_code_map[key] = cmd
                                            self._serial_code_map[key.lower()] = cmd
                                        elif isinstance(key, int):
                                            self._serial_code_map[key] = cmd
                                            self._serial_code_map[f"0x{key:02x}"] = cmd
                                            self._serial_code_map[f"0x{key:02x}".lower()] = cmd
                        
                        # 查找命令：优先使用 code 整数查找，然后尝试 hex 字符串
                        if code is not None:
                            command_name = self._serial_code_map.get(code)
                        if not command_name and hex_val:
                            command_name = self._serial_code_map.get(hex_val) or self._serial_code_map.get(hex_val.lower())
                        
                        if command_name:
                            logger.debug(f"[Command] 从 serial code {hex_val or code} 映射到: {command_name}")
                        else:
                            logger.warning(f"[Command] 未找到 serial code {hex_val or code} 的映射")
                    except Exception as e:
                        logger.error(f"[Command] 加载 serial.yaml 映射失败: {e}", exc_info=True)
        
        logger.info(f"📋 [Command] 处理语音命令: {command_name}")
        
        if not command_name:
            logger.warning(f"⚠️ [Voice] 无法确定命令名，跳过处理 (data={data})")
            return
        
        # ★★★ 统一使用 VoiceCommandManager 处理所有语音命令 ★★★
        if self.voice_manager.handle_command(command_name, **data):
            logger.info(f"✅ [Voice] 指令处理成功: {command_name}")
        else:
            logger.warning(f"⚠️ [Voice] 未找到指令映射或处理失败: {command_name}")

    def _process_mapped_event_candidates(self, candidates, data: Dict[str, Any]) -> None:
        """按候选名称顺序查询映射并执行"""
        logger.debug(f"[MapCommand] 查询candidates: {candidates}")
        try:
            for name in candidates:
                if not name:
                    logger.debug(f"[MapCommand] 跳过空名称")
                    continue
                logger.debug(f"[MapCommand] 查询映射: {name}")
                
                # ★★★ 使用 VoiceCommandManager 替代 CommandMapper ★★★
                cmd_info = self.voice_manager.get_command_info(name)
                if cmd_info:
                    logger.info(f"[MapCommand] 找到匹配: {name} -> {cmd_info.get('action')}")
                    # 使用统一的 handle_command 方法
                    if self.voice_manager.handle_command(name, **data):
                        logger.info(f"[MapCommand] 执行完成: {name}")
                        return
                    else:
                        logger.warning(f"[MapCommand] 执行失败: {name}")
                
            logger.debug(f"[Command] 无匹配映射，跳过 candidates={candidates}")
        except Exception as e:
            logger.error(f"[MapCommand] 异常: {e}", exc_info=True)
    
    # ===== 传感器动作处理器 (for SensorEventManager) =====
    
    def _handle_sensor_action(self, action_type: str, action_config: Dict[str, Any],
                              event_key: str, priority: str = 'NORMAL', **kwargs) -> bool:
        """
        处理传感器动作 (由 SensorEventManager 调用)
        
        Args:
            action_type: 动作类型
            action_config: 动作配置
            event_key: 事件键
            priority: 优先级
            
        Returns:
            是否成功
        """
        try:
            action_data = action_config.get('action', {})
            
            if action_type == 'play_animation':
                animation = action_data.get('animation')
                if not animation:
                    category = action_data.get('category')
                    level = action_data.get('level', 1)
                    # TODO: 从分类和等级选择动画
                    animation = f"{category}.xml"
                
                # 使用 AnimationManager 播放
                anim_priority = 10 if priority == 'CRITICAL' else 5
                return self.animation_manager.play_animation(animation, priority=anim_priority)
            
            elif action_type == 'interrupt_and_play':
                # 中断当前动画并播放新动画
                self.animation_manager.interrupt_all(reason=event_key)
                animation = action_data.get('animation')
                if animation:
                    return self.animation_manager.play_animation(animation, priority=10)
            
            elif action_type == 'blink':
                return self.animation_manager.blink(priority=3)
            
            else:
                logger.warning(f"[Sensor] 未知动作类型: {action_type}")
                return False
                
        except Exception as e:
            logger.error(f"[Sensor] 执行动作失败: {e}", exc_info=True)
            return False
    
    def _handle_tof_event(self, event: Dict[str, Any]) -> bool:
        """处理 TOF 事件 (由 SensorEventManager 调用)"""
        # TOF 事件由 TOF 集成模块处理
        logger.debug(f"[TOF] 事件: {event.get('topic')}")
        return True
    
    def _handle_imu_event(self, event: Dict[str, Any]) -> bool:
        """处理 IMU 事件 (由 SensorEventManager 调用)"""
        logger.debug(f"[IMU] 事件: {event.get('topic')}")
        return True
    
    def _handle_photo_command(self, **kwargs) -> bool:
        """处理拍照指令 (由 VoiceCommandManager 调用)"""
        logger.info("📸 [Photo] 收到拍照指令")
        # 委托给 TimerEventManager 处理
        return self.timer_manager.handle_voice_command('cmd_ActPhoto')
    
    def _handle_timer_event(self, event_type: str, event_data: Optional[Dict[str, Any]] = None) -> bool:
        """处理定时器事件"""
        logger.debug(f"⏰ [Timer] 定时器事件: {event_type}")
        return self.timer_manager.handle_timer_event(event_type, event_data)
    
    # ===== 原有传感器动作执行方法 (保留兼容) =====
    
    def _execute_sensor_action(self, event_name: str, action: Dict[str, Any], event_data: Dict[str, Any]) -> None:
        """
        执行传感器事件的动作
        
        Args:
            event_name: 事件名称
            action: 动作配置（从sensor_event_mapping.yaml读取）
            event_data: 事件数据
        """
        # 检查传感器类型是否启用
        if not self.sensor_config.is_sensor_type_enabled(event_name):
            logger.debug(f"[SensorAction] 传感器类型已禁用，忽略事件: {event_name}")
            return
        
        try:
            action_config = action.get('action', {})
            action_type = action_config.get('type', 'play_animation')
            
            if action_type == 'play_animation':
                # 播放动画
                animation = action_config.get('animation')
                if animation:
                    logger.info(f"[SensorAction] 播放动画: {animation}")
                    self._execute_animation(animation)
                else:
                    logger.warning(f"[SensorAction] 缺少animation字段: {action_config}")
            
            elif action_type == 'play_animation_category':
                # 播放动画分类（随机选择）
                category = action_config.get('category')
                level = action_config.get('level', 1)
                logger.info(f"[SensorAction] 播放动画分类: {category} level={level}")
                # TODO: 从animationlist.xml随机选择该分类的动画
                self._play_animation_category(category, level)
            
            elif action_type == 'direct_command':
                # 执行直接命令
                target = action_config.get('target')
                command = action_config.get('command')
                params = action_config.get('params', {})
                logger.info(f"[SensorAction] 执行命令: {target}.{command}")
                self._execute_direct_command(target, command, params)
            
            elif action_type == 'interrupt_and_play':
                # 中断当前动画然后播放新动画
                animation = action_config.get('animation')
                if self.animation_integration and hasattr(self.animation_integration, 'interrupt_all'):
                    logger.info(f"[SensorAction] 中断并播放: {animation}")
                    self.animation_integration.interrupt_all_sync()
                self._execute_animation(animation)
            
            else:
                logger.warning(f"[SensorAction] 未知action_type: {action_type}")
        
        except Exception as e:
            logger.error(f"[SensorAction] 执行动作异常: {e}", exc_info=True)
    
    def _play_animation_category(self, category: str, level: int = 1) -> None:
        """
        播放某个分类的动画（随机选择）
        
        Args:
            category: 动画分类名称
            level: 分级级别
        """
        # TODO: 实现从animationlist.xml中随机选择动画
        logger.info(f"[Animation] 播放分类动画: {category} level={level}")

    def _execute_action(self, command_name: str, action, data: Dict[str, Any]) -> None:
        """统一执行动作"""
        # 特殊处理：状态转换命令
        if command_name == 'cmd_StaExplore':
            try:
                logger.info("[Command] 进入探索模式")
                self.state_machine.transition_to(DolyState.EXPLORING)
            except Exception as e:
                logger.error(f"[Command] 状态转换失败: {e}")
            return
        
        if action.action_type == 'play_animation':
            self._execute_animation(action.animation, action.audio)
            
        elif action.action_type == 'direct_command':
            params = action.params or {}
            # 将事件数据并入 params.data 供下游参考（不覆盖显式参数）
            enriched_params = {**params}
            if data:
                enriched_params.setdefault('event_data', data)
            logger.debug(f"[Command] direct_command start: {action.target}.{action.command} params={enriched_params}")
            self._execute_direct_command(action.target, action.command, enriched_params)
            logger.debug(f"[Command] direct_command done: {action.target}.{action.command}")
            
        elif action.action_type == 'skill':
            logger.debug(f"[Command] 触发技能: {action.target}.{action.command}")
            
        elif action.action_type == 'dummy':
            logger.debug(f"[Command] dummy 命令: {command_name}")
    
    def _execute_animation(self, animation_file: str, audio_file: str = "") -> None:
        """
        执行动画（异步）
        
        Args:
            animation_file: 动画文件名 (e.g., 'salsa.xml')
            audio_file: 关联的音频文件 (可选)
        """
        logger.info(f"🎬 [Animation] 执行动画: {animation_file}")
        
        # ★★★ 修复：使用 daemon 的主事件循环，避免事件循环不匹配问题 ★★★
        async def run_async_animation():
            try:
                # 获取动画集成实例并播放动画
                anim = await get_animation_integration()
                if not anim:
                    logger.error("[Animation] 无法获取动画系统实例")
                    return
                
                # 如果动画文件包含扩展名，直接作为文件路径
                if animation_file.endswith('.xml'):
                    success = await anim.play_animation_by_file(animation_file)
                    if success:
                        logger.info(f"✅ [Animation] 动画执行完成: {animation_file}")
                    else:
                        logger.warning(f"⚠️ [Animation] 动画执行失败: {animation_file}")
                else:
                    # 否则作为分类名称
                    success = await anim.play_animation_by_category(animation_file, level=1)
                    if success:
                        logger.info(f"✅ [Animation] 分类动画执行完成: {animation_file}")
                    else:
                        logger.warning(f"⚠️ [Animation] 分类动画执行失败: {animation_file}")
                
            except Exception as e:
                logger.error(f"❌ [Animation] 异步执行失败: {e}")
        
        # 将协程提交到 daemon 的主事件循环（避免创建新的事件循环导致冲突）
        if self.loop and self.loop.is_running():
            asyncio.run_coroutine_threadsafe(run_async_animation(), self.loop)
        else:
            logger.warning("[Animation] 事件循环未运行，无法执行动画")
    
    def _execute_direct_command(self, target: str, command: str, params: Dict[str, Any]) -> None:
        """
        执行直接命令
        
        Args:
            target: 目标模块 (eye, drive, audio, system, widget)
            command: 命令名称
            params: 命令参数
        """
        logger.info(f"⚡ [Command] 直接命令: {target}.{command} params={params}")
        
        try:
            ctx = self._zmq_ctx
            sock = ctx.socket(zmq.PUSH)
            sock.setsockopt(zmq.LINGER, 0)
            sock.setsockopt(zmq.SNDTIMEO, 1000)  # 发送超时避免阻塞
            logger.debug(f"[Command] ZMQ socket created for {target}.{command}")
            
            # 根据目标选择端点
            if target == 'eye':
                sock.connect("ipc:///tmp/doly_bus.sock")
                cmd_data = {
                    'type': 'eye_command',
                    'command': command,
                    'params': params
                }
            elif target == 'drive':
                sock.connect("ipc:///tmp/doly_control.sock")
                cmd_data = {
                    'type': 'drive_command',
                    'command': command,
                    'params': params
                }
            elif target == 'audio':
                sock.connect("ipc:///tmp/doly_audio_player_cmd.sock")
                cmd_data = {
                    'type': 'audio_command',
                    'command': command,
                    'params': params
                }
            elif target == 'widget':
                # Widget 服务使用 SUB 模式订阅命令，发送格式为 cmd.widget.<command>
                sock.close()  # PUSH 不适用于 widget，需要用 PUB
                sock = ctx.socket(zmq.PUB)
                sock.setsockopt(zmq.LINGER, 100)
                sock.connect("ipc:///tmp/doly_bus.sock")
                # 等待连接稳定
                import time
                time.sleep(0.05)
                
                # 构造命令话题：cmd.widget.clock.show / cmd.widget.timer.start 等
                topic = f"cmd.widget.{command}"
                cmd_data = {
                    'type': 'widget_command',
                    'command': command,
                    'params': params,
                    'timestamp': time.time()
                }
                import json
                sock.send_multipart([
                    topic.encode('utf-8'),
                    json.dumps(cmd_data).encode('utf-8')
                ])
                logger.debug(f"[Command] Widget 命令已发送: {topic}")
                sock.close()
                return
            else:
                logger.warning(f"[Command] 未知目标: {target}")
                sock.close()
                return

            logger.debug(f"[Command] ZMQ connected endpoint for {target}: {cmd_data}")
            
            import json
            sock.send_json(cmd_data)
            logger.debug(f"[Command] 发送完成 {target}.{command}")
        except Exception as e:
            logger.error(f"[Command] 发送直接命令失败: {e}")
        finally:
            try:
                sock.close()
            except Exception:
                pass
            
    
    def _send_drive_command(self, command: str, params: Dict[str, Any]) -> None:
        """发送底层驱动命令"""
        self._execute_direct_command('drive', command, params)
    
    def start(self) -> bool:
        """启动 Daemon"""
        if self._running:
            logger.warning("[Daemon] 已在运行中")
            return True
        
        try:
            self._running = True
            self._main_thread = threading.Thread(
                target=self._main_loop,
                name="Daemon-Main",
                daemon=False
            )
            self._main_thread.start()
            
            logger.info("🚀 [Daemon] 启动成功")
            return True
            
        except Exception as e:
            logger.error(f"❌ [Daemon] 启动失败: {e}")
            self._running = False
            return False
    
    def _main_loop(self) -> None:
        """主循环"""
        logger.info("[Daemon] 主循环启动")
        
        loop_count = 0
        # IDLE 状态下眨眼/表情动画定时器
        next_idle_anim_time = time.time() + 3 + (5 * (time.time() % 1))  # 初始3-8秒
        import random
        while self._running:
            try:
                # 更新状态机（检查超时）
                self.state_machine.update()
                
                # 定期输出统计信息
                loop_count += 1
                if loop_count % 100 == 0:
                    state_info = self.state_machine.get_state_info()
                    logger.debug(f"[Daemon] 状态信息: {state_info} | 事件数: {self._event_count} | 命令数: {self._command_count}")

                # 检查睡眠时段并可能切换状态
                try:
                    now_local = time.localtime()
                    hhmm = f"{now_local.tm_hour:02d}:{now_local.tm_min:02d}"
                    in_sleep = False
                    for (s, e) in getattr(self, '_sleep_schedules', []):
                        # 处理跨日区间
                        if s <= e:
                            if s <= hhmm < e:
                                in_sleep = True
                                break
                        else:
                            # 跨日，例如 23:00-07:00
                            if hhmm >= s or hhmm < e:
                                in_sleep = True
                                break
                    # 根据判断切换状态
                    if in_sleep and self.state_machine.current_state != DolyState.SLEEPING:
                        logger.info("[Daemon] 当前时间处于睡眠时段，切换到 SLEEPING")
                        self.state_machine.transition_to(DolyState.SLEEPING)
                    elif not in_sleep and self.state_machine.current_state == DolyState.SLEEPING:
                        logger.info("[Daemon] 当前时间不在睡眠时段，退出 SLEEPING 回到 IDLE")
                        self.state_machine.transition_to(DolyState.IDLE)
                except Exception:
                    pass

                # 非交互超时进入短睡（snooze）逻辑：
                # 不改变 state，只执行配置中的 snooze_action
                # ★ 改进：基于 seq 动画完成事件追踪，而不是固定时间冷却
                try:
                    # 仅当在自主模式且不处于 schedule sleep 时考虑 snooze
                    if self.state_machine.current_state != DolyState.SLEEPING:
                        state_name = self.state_machine.current_state.value
                        cfg = self.state_behaviors.get(state_name) if isinstance(self.state_behaviors, dict) else None
                        if cfg and isinstance(cfg, dict):
                            # 支持新的命名 snooze_after/snooze_action，同时向后兼容 nap_*
                            snooze_after = cfg.get('snooze_after', cfg.get('nap_after', 0))
                            now = time.time()
                            # Respect per-state enabled flag
                            state_enabled = bool(cfg.get('enabled', True))
                            logger.debug(f"[Daemon][SNOOZE] state={state_name} enabled={state_enabled} snooze_after={snooze_after} last_interaction={getattr(self,'last_interaction_time',0)}")
                            if not state_enabled:
                                # 状态被禁用，跳过 snooze
                                logger.debug(f"[Daemon][SNOOZE] state {state_name} disabled, skipping snooze check")
                            else:
                                snooze_after = float(snooze_after or 0)
                                if snooze_after and (now - getattr(self, 'last_interaction_time', 0)) >= snooze_after:
                                    # 检查是否已经有 snooze 在进行中
                                    # 只有当前一个 snooze 已经完成后，才能触发下一个
                                    logger.debug(f"[Daemon][SNOOZE] snooze_in_progress={self._snooze_in_progress}")
                                    if self._snooze_in_progress:
                                        logger.debug("[Daemon][SNOOZE] snooze animation is still playing, skip")
                                    else:
                                        # 触发 snooze_action（不改变主状态），优先使用 state 下的 snooze_action，否则使用 sleep.entry_action
                                        snooze_action = cfg.get('snooze_action', cfg.get('nap_action')) if isinstance(cfg, dict) else None
                                        if not snooze_action:
                                            sleep_cfg = self.state_behaviors.get('sleep') if isinstance(self.state_behaviors, dict) else None
                                            if not sleep_cfg:
                                                sleep_cfg = self.state_behaviors.get('sleep_state') if isinstance(self.state_behaviors, dict) else None
                                            if sleep_cfg:
                                                snooze_action = sleep_cfg.get('entry_action')

                                        logger.debug(f"[Daemon][SNOOZE] found snooze_action={snooze_action}")

                                        if snooze_action is None:
                                            logger.debug("[Daemon][SNOOZE] no snooze_action configured, skipping")
                                        elif getattr(self, 'actions_muted', False):
                                            logger.info("[Daemon][SNOOZE] actions_muted=True, skipping snooze_action playback")
                                        else:
                                            # 如果是 animation，则播放
                                            try:
                                                if snooze_action.get('type') == 'animation':
                                                    category = snooze_action.get('category')
                                                    level = snooze_action.get('level', 1)
                                                    logger.info(f"[Daemon][SNOOZE] 触发 snooze 动画: {category} (level={level})")
                                                    if not self.animation_integration:
                                                        logger.warning("[Daemon][SNOOZE] animation_integration 未初始化，无法播放 snooze 动画")
                                                    else:
                                                        categories = self.animation_integration.get_categories()
                                                        logger.debug(f"[Daemon][SNOOZE] available categories: {categories}")
                                                        if category in categories:
                                                            # 标记 snooze 正在进行，等待 overlay.completed 事件来重置
                                                            self._snooze_in_progress = True
                                                            coro = self.animation_integration.play_animation_by_category(category, level, random_select=True)
                                                            if asyncio.iscoroutine(coro):
                                                                self.loop.create_task(coro)
                                                        else:
                                                            logger.warning(f"[Daemon][SNOOZE] 动画分类 {category} 在动画系统中不存在")
                                                else:
                                                    logger.debug(f"[Daemon][SNOOZE] unsupported snooze_action type: {snooze_action.get('type')}")
                                            except Exception as e:
                                                logger.warning(f"[Daemon][SNOOZE] 执行 snooze_action 失败: {e}")
                                                self._snooze_in_progress = False
                except Exception as e:
                    logger.exception(f"[Daemon][SNOOZE] 检查/触发异常: {e}")

                # IDLE 状态下定时触发眨眼/表情动画
                if self.state_machine.current_state == DolyState.IDLE and self.animation_integration:
                    now = time.time()
                    if now >= next_idle_anim_time:
                        # 从配置中选取当前状态的动作权重表
                        state_name = self.state_machine.current_state.value
                        cfg = self.state_behaviors.get(state_name)
                        categories = self.animation_integration.get_categories()

                        chosen = None
                        # 如果配置存在，优先尊重顶层 enabled 开关：
                        # 当 state.enabled 为 False 时，跳过整个 actions 清单（不再逐项检查）
                        if cfg and isinstance(cfg, dict):
                            state_enabled = bool(cfg.get('enabled', True))
                            logger.debug(f"[IDLE] state={state_name} enabled={state_enabled}")
                            if not state_enabled:
                                logger.info(f"[IDLE] 状态 {state_name} 被禁用，跳过自主动作触发")
                            else:
                                actions = cfg.get('actions', [])
                                # 支持顶层 actions 开关：
                                # - cfg.actions_enabled == False -> 跳过整个 actions
                                # - cfg.actions_enabled == True  -> 忽略单个 action.enabled，整体启用
                                # - cfg.actions_enabled missing -> 以每个 action.enabled 字段为准
                                actions_enabled_flag = cfg.get('actions_enabled', None)
                                if actions_enabled_flag is False:
                                    logger.info(f"[IDLE] 状态 {state_name} 的 actions 被显式禁用(actions_enabled=False)，跳过")
                                else:
                                    weighted = []
                                    for a in actions:
                                        # 决定此 action 是否可用
                                        if actions_enabled_flag is True:
                                            a_enabled = True
                                        else:
                                            a_enabled = bool(a.get('enabled', True))
                                        if not a_enabled:
                                            continue
                                        if getattr(self, 'actions_muted', False):
                                            # 全局静音，跳过所有表达型 actions
                                            continue
                                        if a.get('type') == 'animation' and a.get('category') in categories:
                                            weighted.extend([a] * max(1, int(a.get('weight', 1))))
                                    if weighted:
                                        chosen = random.choice(weighted)

                        # 如果配置没有命中且没有任何 state 配置存在，降级回旧逻辑（从 animationlist 中随机选几个常见分类）
                        if not chosen and not cfg:
                            fallback = [
                                {'type': 'animation', 'category': 'ANIMATION_BORED', 'level': 1},
                                {'type': 'animation', 'category': 'ANIMATION_HAPPY', 'level': 1},
                                {'type': 'animation', 'category': 'ANIMATION_PETTING', 'level': 1},
                            ]
                            fallback = [a for a in fallback if a['category'] in categories]
                            if fallback:
                                chosen = random.choice(fallback)

                        if chosen and chosen.get('type') == 'animation':
                            category = chosen.get('category')
                            level = chosen.get('level', 1)
                            logger.info(f"[IDLE] 触发自主动画(配置驱动): {category} (level={level})")
                            try:
                                coro = self.animation_integration.play_animation_by_category(category, level, random_select=True)
                                if asyncio.iscoroutine(coro):
                                    self.loop.create_task(coro)
                            except Exception as e:
                                logger.warning(f"[IDLE] 动画触发异常: {e}")

                        # 计算下次触发时间（优先使用配置中的 min/max，否则使用默认 3-8）
                        if cfg and isinstance(cfg, dict):
                            min_i = float(cfg.get('min_interval', 3))
                            max_i = float(cfg.get('max_interval', 8))
                        else:
                            min_i = 3.0
                            max_i = 8.0
                        next_idle_anim_time = now + random.uniform(min_i, max_i)

                time.sleep(0.1)  # 10Hz 更新频率
                
            except Exception as e:
                logger.error(f"[Daemon] 主循环异常: {e}")
        
        logger.info("[Daemon] 主循环结束")
    
    def stop(self) -> None:
        """停止 Daemon"""
        logger.info("[Daemon] 停止中...")
        
        self._running = False
        
        # 停止订阅器
        if self._serial_subscriber:
            self._serial_subscriber.stop()
        if self._sensor_subscriber:
            self._sensor_subscriber.stop()
        if getattr(self, '_eye_engine_subscriber', None):
            self._eye_engine_subscriber.stop()
        if self._widget_subscriber:
            self._widget_subscriber.stop()
        if getattr(self, '_vision_subscriber', None):
            self._vision_subscriber.stop()
        
        # 停止事件总线
        self.event_bus.stop()
        
        # 等待主线程
        if self._main_thread and self._main_thread.is_alive():
            self._main_thread.join(timeout=5.0)
        
        logger.info("✅ [Daemon] 已停止")
    
    def get_status(self) -> Dict[str, Any]:
        """获取状态信息"""
        return {
            'running': self._running,
            'state': self.state_machine.get_state_info(),
            'event_count': self._event_count,
            'command_count': self._command_count,
        }
    
    def unlock_command(self, command_name: str) -> None:
        """
        手动解除命令锁定
        
        用于在长时任务提前完成时解除锁定，允许重新触发
        
        Args:
            command_name: 要解锁的命令名称
        """
        if command_name in self._command_locks:
            del self._command_locks[command_name]
            logger.info(f"[Voice] 🔓 手动解除命令锁定: {command_name}")
        else:
            logger.debug(f"[Voice] 命令未锁定，无需解锁: {command_name}")


def main():
    """主函数"""
    import argparse
    
    parser = argparse.ArgumentParser(description='Doly Daemon')
    parser.add_argument('--config', type=str, default='/home/pi/dolydev/config',
                        help='配置目录路径')
    parser.add_argument('--debug', action='store_true', help='调试模式')
    args = parser.parse_args()
    
    # 设置日志级别
    if args.debug:
        logging.getLogger().setLevel(logging.DEBUG)
    
    logger.info("=" * 60)
    logger.info("🤖 Doly Daemon 启动")
    logger.info("=" * 60)
    
    # 创建并初始化 Daemon
    daemon = DolyDaemon(config_dir=args.config)
    
    if not daemon.initialize():
        logger.error("❌ Daemon 初始化失败")
        return 1
    
    # 启动 Daemon
    if not daemon.start():
        logger.error("❌ Daemon 启动失败")
        return 1
    
    # ★ 在主线程中运行 event loop（这样 async 操作可以正确执行）
    try:
        # 主线程保活，同时运行事件循环以支持 asyncio 操作
        while daemon._running:
            try:
                # 运行一次 event loop 迭代（非阻塞）
                daemon.loop.run_until_complete(asyncio.sleep(0.1))
            except KeyboardInterrupt:
                raise
            except Exception as e:
                logger.error(f"[MainLoop] Event loop 异常: {e}")
            
    except KeyboardInterrupt:
        logger.info("\n⏹️ 收到中断信号")
    
    finally:
        daemon.stop()
        logger.info("=" * 60)
        logger.info("🤖 Doly Daemon 已关闭")
        logger.info("=" * 60)


if __name__ == '__main__':
    sys.exit(main())
