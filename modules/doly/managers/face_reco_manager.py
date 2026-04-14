"""
人脸识别事件管理器

处理 Vision Service 的人脸事件，并触发相应的机器人行为。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import time
import logging
import threading
from typing import Optional, Dict, Any, Callable, List
from dataclasses import dataclass, field
from enum import Enum
from pathlib import Path

import yaml

logger = logging.getLogger(__name__)


class FaceRelation(Enum):
    """人脸关系类型"""
    MASTER = "master"           # 主人
    FAMILY = "family"           # 家人
    FRIEND = "friend"           # 朋友
    ACQUAINTANCE = "acquaintance"  # 熟人
    STRANGER = "stranger"       # 陌生人
    UNKNOWN = "unknown"         # 未知


@dataclass
class FaceProfile:
    """人脸档案"""
    face_id: str
    name: str
    relation: FaceRelation = FaceRelation.UNKNOWN
    last_seen_ms: int = 0
    last_greeted_ms: int = 0
    greeting_count: int = 0
    metadata: Dict[str, Any] = field(default_factory=dict)


@dataclass
class TrackedFace:
    """当前跟踪的人脸"""
    tracker_id: int
    face_id: Optional[str] = None
    name: str = "unknown"
    relation: FaceRelation = FaceRelation.UNKNOWN
    bbox: Dict[str, float] = field(default_factory=dict)
    normalized: Dict[str, float] = field(default_factory=dict)
    confidence: float = 0.0
    liveness: bool = False
    angle: float = 0.0
    first_seen_ms: int = 0
    last_seen_ms: int = 0
    recognized: bool = False
    greeted: bool = False


class FaceRecoManager:
    """
    人脸识别事件管理器
    
    职责：
    - 订阅 Vision Service 的人脸事件
    - 维护当前跟踪的人脸列表
    - 根据配置触发行为（动画、语音、动作）
    - 管理人脸注册流程
    - 实现眼神跟随逻辑
    """
    
    def __init__(self, config_path: Optional[str] = None):
        """
        初始化人脸识别管理器
        
        Args:
            config_path: 配置文件路径
        """
        self.config_path = Path(config_path or 
            "/home/pi/dolydev/config/face_reco_settings.yaml")
        
        # 配置
        self.config: Dict[str, Any] = {}
        self.enabled: bool = True
        
        # 人脸数据
        self.known_faces: Dict[str, FaceProfile] = {}
        self.current_faces: Dict[int, TrackedFace] = {}
        self.primary_face_id: Optional[int] = None
        
        # 状态
        self.last_greet_time: Dict[str, float] = {}  # face_id -> 上次打招呼时间 (deprecated, 用 recognition_history)
        self.recognition_history: Dict[str, Dict[str, Any]] = {}  # 识别历史: name -> {last_seen, last_liveness, greeted}
        self.recognition_cooldown: float = 5.0
        self.recognition_timeout: int = 30
        self.service_connected: bool = False
        self._lock = threading.RLock()
        
        # 外部依赖（由 daemon 设置）
        self.daemon = None  # ★ Daemon 实例（用于解锁命令等）
        self.animation_manager = None
        self.tts_client = None
        self.eye_client = None
        self.led_controller = None
        self.state_provider: Optional[Callable[[], str]] = None
        self.zmq_publisher = None
        self.vision_mode_manager = None  # VisionModeManager 实例
        
        # ★★★ 新增：眼神跟踪器 ★★★
        self.gaze_tracker = None  # 稍后在配置加载后初始化
        
        # ★★★ 新增：人脸管理 ★★★
        self.confirmation_handler = None  # ConfirmationHandler 实例（异步确认）
        # self.face_db_client = None  # ❌ 已删除：改为通过 ZMQ 与 Vision Service 通信
        self.last_detected_face: Optional[TrackedFace] = None  # 最后检测到的人脸（用于注册）
        
        # ★★★ 新增：强制打招呼标志 ★★★
        self.force_greet_next_recognition: bool = False  # 用于 cmd_ActWhoami 场景，绕过冷却时间
        self._last_new_face_prompt_ms: int = 0  # 新人脸提示节流，避免跟踪抖动导致重复播报
        
        # ★★★ 新增：人脸注册流程控制 ★★★
        self.pending_register_command: bool = False  # 是否有待处理的注册命令
        # 回调
        self.on_face_recognized: Optional[Callable[[TrackedFace], None]] = None
        self.on_face_lost: Optional[Callable[[TrackedFace], None]] = None
        self.on_new_face: Optional[Callable[[TrackedFace], None]] = None
        self.on_all_faces_lost: Optional[Callable[[], None]] = None
        
        # 加载配置
        self._load_config()
        
        logger.info("✅ [FaceRecoManager] 初始化完成")
    
    def _load_config(self) -> None:
        """加载配置文件"""
        try:
            if not self.config_path.exists():
                logger.warning(f"[FaceRecoManager] 配置文件不存在: {self.config_path}")
                self._use_default_config()
                return
            
            with open(self.config_path, 'r', encoding='utf-8') as f:
                import yaml
                self.config = yaml.safe_load(f) or {}
            
            # 提取配置
            face_config = self.config.get('face_recognition', {})
            logger.info(f"[FaceRecoManager] 🔍 配置调试: face_config keys={list(face_config.keys())}")
            
            self.enabled = face_config.get('enabled', True)
            self.auto_greet = face_config.get('auto_greet', True)
            self.greet_cooldown = face_config.get('greet_cooldown_seconds', 300)
            self.recognition_threshold = face_config.get('recognition_threshold', 0.85)
            self.liveness_required = face_config.get('liveness_required', True)
            self.liveness_change_alert = face_config.get('liveness_change_alert', True)  # 活体变化提醒
            mode_config = face_config.get('mode', {})
            self.recognition_timeout = mode_config.get('recognition_timeout', 30)
            self.recognition_cooldown = float(mode_config.get('recognition_cooldown_seconds', 5))
            self.name_to_relation = face_config.get('name_to_relation', {})  # 姓名到角色映射
            logger.info(f"[FaceRecoManager] 🔍 name_to_relation 原始值: {self.name_to_relation}")
            self.default_relation = face_config.get('default_relation', 'stranger')  # 默认关系
            
            self.new_face_config = face_config.get('new_face_behavior', {})
            self.known_face_behaviors = face_config.get('known_face_behaviors', {})
            self.face_lost_config = face_config.get('face_lost_behaviors', {})
            self.gaze_config = face_config.get('gaze_follow', {})
            
            vision_config = self.config.get('vision_service', {})
            self.ignore_states = set(face_config.get('ignore_in_states', []))
            
            # ★★★ 初始化眼神跟踪器 ★★★
            gaze_tracking_config = face_config.get('gaze_tracking', {})
            if gaze_tracking_config.get('enabled', True):
                from modules.doly.managers.gaze_tracker import GazeTracker
                self.gaze_tracker = GazeTracker(gaze_tracking_config)
                logger.info("[FaceRecoManager] ✅ 眼神跟踪器已启用")
            else:
                self.gaze_tracker = None
                logger.info("[FaceRecoManager] ⚠️ 眼神跟踪器已禁用")
            
            # ★★★ 初始化确认处理器（人脸管理用） ★★★
            face_mgmt_config = self.config.get('face_management', {})
            confirmation_config = face_mgmt_config.get('confirmation', {})
            if confirmation_config.get('enabled', True):
                from modules.doly.managers.confirmation_handler import ConfirmationHandler
                # TTS 客户端将在 set_tts_client 中设置
                self.confirmation_handler = ConfirmationHandler(tts_client=None)
                logger.info("[FaceRecoManager] ✅ 确认处理器已初始化")
            else:
                self.confirmation_handler = None
                logger.info("[FaceRecoManager] ⚠️ 确认处理器已禁用")
            
            # ★★★ FaceDB 客户端已移除 ★★★
            # 现在通过 ZMQ 命令直接与 Vision Service 的 FaceDatabase 通信
            # 不再需要在 Daemon 端维护独立的数据库客户端
            
            logger.info(f"[FaceRecoManager] 配置加载成功，姓名映射数量: {len(self.name_to_relation)}")
            
        except Exception as e:
            logger.error(f"[FaceRecoManager] 配置加载失败: {e}", exc_info=True)
            self._use_default_config()
    
    def _use_default_config(self) -> None:
        """使用默认配置"""
        self.enabled = True
        self.auto_greet = True
        self.greet_cooldown = 300
        self.recognition_cooldown = 5.0
        self.recognition_timeout = 30
        self.recognition_threshold = 0.85
        self.liveness_required = True
        self.liveness_change_alert = True  # 默认开启活体变化提醒
        self.name_to_relation = {}  # 默认空映射
        self.default_relation = 'stranger'  # 默认为陌生人
        self.new_face_config = {'enabled': True, 'prompt_registration': True}
        self.known_face_behaviors = {
            'master': {'greeting_animation': 'HAPPY.happy_excited', 'priority': 8},
            'family': {'greeting_animation': 'HAPPY.happy_normal', 'priority': 7},
            'friend': {'greeting_animation': 'NEUTRAL.neutral_blink', 'priority': 6},
            'acquaintance': {'greeting_animation': 'NEUTRAL.neutral_normal', 'priority': 5},
            'stranger': {'greeting_animation': 'CURIOUS.curious_look', 'priority': 4}
        }
        self.face_lost_config = {'delay_seconds': 2.0}
        self.gaze_config = {'enabled': True, 'smoothing': 0.15}
        self.ignore_states = {'sleeping', 'shutdown'}
        logger.info("[FaceRecoManager] 使用默认配置")
    
    # ========== 外部依赖设置 ==========
    
    def set_animation_manager(self, manager) -> None:
        """设置动画管理器"""
        self.animation_manager = manager
        logger.debug("[FaceRecoManager] 已设置动画管理器")
        
    def set_tts_client(self, client) -> None:
        """设置 TTS 客户端"""
        self.tts_client = client
        logger.debug("[FaceRecoManager] 已设置 TTS 客户端")
        
    def set_eye_client(self, client) -> None:
        """设置眼睛引擎客户端"""
        self.eye_client = client
        logger.debug("[FaceRecoManager] 已设置眼睛引擎客户端")
        
    def set_led_controller(self, controller) -> None:
        """设置 LED 控制器"""
        self.led_controller = controller
        logger.debug("[FaceRecoManager] 已设置 LED 控制器")
        
    def set_state_provider(self, provider: Callable[[], str]) -> None:
        """设置状态提供器"""
        self.state_provider = provider
        logger.debug("[FaceRecoManager] 已设置状态提供器")
        
    def set_zmq_publisher(self, publisher) -> None:
        """设置 ZMQ 发布器（用于发送命令到 Vision Service）"""
        self.zmq_publisher = publisher
        logger.debug("[FaceRecoManager] 已设置 ZMQ 发布器")
        # 注意：FaceDB 客户端已移除，所有人脸管理操作通过 ZMQ 命令发送到 Vision Service
        # 如果配置文件中仍包含 'face_db' 配置段，请将其删除或忽略
    
    def set_vision_mode_manager(self, manager) -> None:
        """设置 Vision 模式管理器"""
        # 延迟导入避免循环依赖（VisionModeManager 类型提示仅用于 IDE）
        self.vision_mode_manager = manager
        logger.debug("[FaceRecoManager] 已设置 Vision 模式管理器")
    
    # ========== 语音命令处理 ==========
    
    def handle_whoami_command(self) -> bool:
        """
        处理 "看看我是谁" 语音命令（cmd_ActWhoami, 0x07）
        
        流程：
        1. 切换到 FULL 模式（完整识别模式）
        2. 启动超时计时器（30秒默认）
        3. 等待识别结果
        4. 识别成功后触发问候
        5. 超时或识别完成后自动回 IDLE
        
        Returns:
            是否成功切换模式
        """
        logger.info("[FaceRecoManager] 🎤 收到语音命令: cmd_ActWhoami (看看我是谁)")
        
        if not self.vision_mode_manager:
            logger.warning("[FaceRecoManager] VisionModeManager 未设置，无法切换模式")
            # 播放提示语音
            if self.tts_client:
                self.tts_client.speak("抱歉，人脸识别功能暂时不可用")
            return False
        
        # 获取配置的超时时间
        timeout = getattr(self, 'recognition_timeout', 30)
        
        # ★★★ 设置强制打招呼标志（绕过冷却时间限制）★★★
        self.force_greet_next_recognition = True
        logger.info("[FaceRecoManager] 💪 设置强制打招呼标志，下次识别将绕过冷却时间")
        
        # ★★★ 切换到 FULL 模式（完整识别） ★★★
        success = self.vision_mode_manager.set_mode('FULL', timeout=timeout)
        
        if success:
            logger.info(f"[FaceRecoManager] ✅ 已切换到 FULL 模式（{timeout}秒超时）")
            
            # 播放提示语音
            if self.tts_client:
                self.tts_client.speak("好的，让我看看你是谁")
            
            # ★★★ 启动保底解锁计时器（超时+5秒后强制解锁，防止卡死）★★★
            def force_unlock():
                if self.daemon and hasattr(self.daemon, 'unlock_command'):
                    self.daemon.unlock_command('cmd_ActWhoami')
                    logger.info("[FaceRecoManager] ⏰ 保底解锁：cmd_ActWhoami 超时强制解锁")
            
            threading.Timer(timeout + 5.0, force_unlock).start()
            
        else:
            logger.error("[FaceRecoManager] ❌ 模式切换失败")
            # 重置标志
            self.force_greet_next_recognition = False
            
            # 播放错误提示
            if self.tts_client:
                self.tts_client.speak("抱歉，识别功能启动失败")
        
        return success
    
    def handle_take_photo_command(self, **kwargs) -> bool:
        """
        处理拍照命令（cmd_ActTakePhoto, 0x0D）
        
        流程：
        1. 切换到 STREAM_ONLY 模式（仅采集，不做人脸识别）
        2. 发送拍照命令到 FaceReco（通过 ZMQ）
        3. 完成后自动回 IDLE
        
        Returns:
            是否成功
        """
        logger.info("[FaceRecoManager] 📸 收到语音命令: cmd_ActTakePhoto (拍照)")
        
        if not self.vision_mode_manager:
            logger.warning("[FaceRecoManager] VisionModeManager 未设置")
            if self.tts_client:
                self.tts_client.speak("拍照功能暂时不可用")
            return False
        
        # 切换到 STREAM_ONLY 模式，确保相机活跃但不触发人脸识别
        success = self.vision_mode_manager.set_mode('STREAM_ONLY', timeout=10)
        
        if success:
            logger.info("[FaceRecoManager] ✅ 拍照模式已启动")
            
            # 播放提示
            if self.tts_client:
                self.tts_client.speak("准备拍照，请看镜头")
            
            # ✅ 发送拍照指令到 Vision Service
            if not self.zmq_publisher:
                logger.error("[FaceRecoManager] ❌ ZMQ Publisher 未初始化，无法发送拍照命令")
                if self.tts_client:
                    self.tts_client.speak("拍照失败，请重试")
                return False
            
            try:
                capture_command = {
                    "request_id": f"photo_{int(time.time() * 1000)}",
                    "type": "photo",
                    "save_snapshot": True
                }
                
                logger.info(f"[FaceRecoManager] 📤 发送拍照命令到 FaceReco: {capture_command}")
                self.zmq_publisher.publish_command(
                    topic="cmd.vision.capture.photo",
                    data=capture_command
                )
                
                logger.info("[FaceRecoManager] ✅ 拍照命令已发送到 FaceReco")
                
            except Exception as e:
                logger.error(f"[FaceRecoManager] ❌ 发送拍照命令失败: {e}", exc_info=True)
                if self.tts_client:
                    self.tts_client.speak("拍照失败，请重试")
                return False
        else:
            logger.error("[FaceRecoManager] ❌ 拍照模式启动失败")
            if self.tts_client:
                self.tts_client.speak("拍照失败")
        
        return success
    
    def handle_take_video_command(self, duration: int = 10, **kwargs) -> bool:
        """
        处理录像命令（cmd_ActTakeVideo, 0x0E）
        
        Args:
            duration: 录像时长（秒）
            **kwargs: 其他参数
        
        Returns:
            是否成功
        """
        logger.info(f"[FaceRecoManager] 🎥 收到语音命令: cmd_ActTakeVideo (录像 {duration}秒)")
        
        # 获取录像配置
        video_config = self.config.get('face_recognition', {}).get('video', {})
        max_duration = video_config.get('max_duration', 60)
        enable_stream_preview = video_config.get('enable_stream_preview', True)
        disable_face_tracking = video_config.get('disable_face_tracking', True)
        
        # 限制录像时长
        if duration > max_duration:
            logger.warning(f"[FaceRecoManager] ⚠️ 录像时长超过限制，调整为 {max_duration} 秒")
            duration = max_duration
        
        # ★★★ 根据配置决定是否需要人脸跟踪 ★★★
        if not disable_face_tracking and self.vision_mode_manager:
            # 需要人脸跟踪：切换到 DETECT_TRACK 模式（不会执行人脸识别，只做检测+跟踪）
            timeout = duration + 5
            success = self.vision_mode_manager.set_mode('DETECT_TRACK', timeout=timeout)
            if not success:
                logger.error("[FaceRecoManager] ❌ 切换到 DETECT_TRACK 模式失败")
                if self.tts_client:
                    self.tts_client.speak("录像模式启动失败")
                return False
            logger.info(f"[FaceRecoManager] ✅ 已切换到 DETECT_TRACK 模式（{duration}秒）")
        else:
            # 不需要人脸跟踪：切换到 STREAM_ONLY，避免录像期间触发人脸识别逻辑
            if self.vision_mode_manager:
                timeout = duration + 5
                success = self.vision_mode_manager.set_mode('STREAM_ONLY', timeout=timeout)
                if not success:
                    logger.error("[FaceRecoManager] ❌ 切换到 STREAM_ONLY 模式失败")
                    if self.tts_client:
                        self.tts_client.speak("录像模式启动失败")
                    return False
            logger.info(f"[FaceRecoManager] 📹 录像模式（无人脸跟踪）已启动（{duration}秒）")
        
        # 播放提示
        if self.tts_client:
            self.tts_client.speak(f"开始录像，时长{duration}秒")
        
        # ✅ 发送录视开始指令到 Vision Service
        if not self.zmq_publisher:
            logger.error("[FaceRecoManager] ❌ ZMQ Publisher 未初始化，无法发送录视命令")
            if self.tts_client:
                self.tts_client.speak("录视失败，请重试")
            return False
        
        try:
            request_id = f"video_{int(time.time() * 1000)}"
            # ★★★ 仅发送基础参数给 FaceReco（新参数暂留在 daemon 侧处理）★★★
            video_start_command = {
                "request_id": request_id,
                "duration": duration,
                "type": "video_start"
            }
            
            logger.info(f"[FaceRecoManager] 📤 发送录视开始命令到 FaceReco: {video_start_command}")
            logger.info(f"[FaceRecoManager] � 录像配置: enable_stream_preview={enable_stream_preview}, disable_face_tracking={disable_face_tracking}")
            self.zmq_publisher.publish_command(
                topic="cmd.vision.capture.video.start",
                data=video_start_command
            )
            
            logger.info("[FaceRecoManager] ✅ 录视开始命令已发送到 FaceReco")
            
            # ✅ 设置强力定时器自动停止录像（使用线程池，避免被主线程堵住）
            def stop_video_recording():
                try:
                    # 延迟一段时间后停止
                    time.sleep(duration)
                    
                    logger.info(f"[FaceRecoManager] ⏱️ 录像时长到期，发送停止命令 (request_id={request_id})")
                    video_stop_command = {
                        "request_id": request_id,
                        "type": "video_stop"
                    }
                    
                    # 确保 ZMQ 发布器可用
                    if not self.zmq_publisher:
                        logger.error(f"[FaceRecoManager] ❌ ZMQ Publisher 不可用，无法停止录像")
                        return
                    
                    self.zmq_publisher.publish_command(
                        topic="cmd.vision.capture.video.stop",
                        data=video_stop_command
                    )
                    logger.info("[FaceRecoManager] ✅ 录像停止命令已发送")
                except Exception as e:
                    logger.error(f"[FaceRecoManager] ❌ 发送录像停止命令失败: {e}", exc_info=True)
            
            # ★★★ 使用线程池而不是 Timer，确保不被主线程堵住 ★★★
            import concurrent.futures
            if not hasattr(self, '_executor'):
                self._executor = concurrent.futures.ThreadPoolExecutor(max_workers=2, thread_name_prefix='VideoTimer')
            
            # 提交到线程池
            self._executor.submit(stop_video_recording)
            logger.info(f"[FaceRecoManager] ⏱️ 已在后台线程设置 {duration} 秒后自动停止录像")
            
        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 发送录视命令失败: {e}", exc_info=True)
            if self.tts_client:
                self.tts_client.speak("录视失败，请重试")
            return False
        
        return True
    
    # ========== 人脸管理方法 (Face CRUD) ==========
    
    def handle_register_face_command(self, command_data: Optional[Dict] = None) -> bool:
        """
        处理注册人脸命令（cmd_RegisterFace） - 改进版
        
        工作流程:
        1. 切换到 FULL 模式开始人脸检测和识别
        2. 等待检测到人脸（异步，通过标志控制）
        3. 检测到人脸后执行注册
        4. 超时或成功后自动回 IDLE
        
        Args:
            command_data: 命令数据（可能包含 name）
            
        Returns:
            是否成功发起流程
        """
        logger.info("[FaceRecoManager] 📝 收到语音命令: cmd_RegisterFace (注册人脸)")
        
        # 检查 Vision 模式管理器
        if not self.vision_mode_manager:
            logger.error("[FaceRecoManager] ❌ VisionModeManager 未设置")
            if self.tts_client:
                self.tts_client.speak("人脸识别功能未启用")
            return False
        
        # 检查 ZMQ 发布器
        if not self.zmq_publisher:
            logger.error("[FaceRecoManager] ❌ ZMQ Publisher 未初始化")
            if self.tts_client:
                self.tts_client.speak("系统错误，请稍后重试")
            return False
        
        # 取消之前的注册超时计时器（如果有）
        if self.register_timeout_timer:
            self.register_timeout_timer.cancel()
            self.register_timeout_timer = None
        
        # 提取姓名（如果有）
        command_data = command_data or {}
        self.register_name = command_data.get('name', '新朋友')
        
        # 播放提示
        if self.tts_client:
            self.tts_client.speak(f"准备注册{self.register_name}的人脸，请看镜头")
        
        # 步骤 1: 切换到 FULL 模式，启动人脸检测和识别
        success = self.vision_mode_manager.set_mode('FULL', timeout=30)
        
        if not success:
            logger.error("[FaceRecoManager] ❌ 切换到识别模式失败")
            if self.tts_client:
                self.tts_client.speak("识别模式启动失败")
            return False
        
        logger.info("[FaceRecoManager] ✅ 已切换到 FULL 模式，等待检测人脸...")
        
        # 步骤 2: 设置标志，等待检测到人脸
        self.pending_register_command = True
        
        # 步骤 3: 启动超时计时器 (30秒)
        def on_register_timeout():
            logger.warning("[FaceRecoManager] ⏰ 注册超时，未检测到人脸")
            self.pending_register_command = False
            self.register_name = None
            
            # TTS 提示
            if self.tts_client:
                self.tts_client.speak("注册超时，未检测到人脸")
            
            # 切回 IDLE
            if self.vision_mode_manager:
                self.vision_mode_manager.set_mode('IDLE')
            
            # 解锁命令
            if self.daemon and hasattr(self.daemon, 'unlock_command'):
                self.daemon.unlock_command('cmd_RegisterFace')
        
        self.register_timeout_timer = threading.Timer(30.0, on_register_timeout)
        self.register_timeout_timer.start()
        
        logger.info("[FaceRecoManager] ⏰ 注册超时计时器已启动 (30秒)")
        
        return True
    
    def _execute_register_face(self, operation: Dict[str, Any]) -> bool:
        """
        执行注册人脸（确认后调用） - 通过 ZMQ 命令

        Args:
            operation: 操作数据 {'data': {...}}

        Returns:
            是否成功
        """
        try:
            data = operation.get('data', {}) if isinstance(operation, dict) else {}
            name = data.get('name', self.register_name or '新朋友')

            logger.info(f"[FaceRecoManager] 🔄 执行人脸注册: {name}")

            # ✅ 新架构：通过 ZMQ 命令发送给 Vision Service
            if not self.zmq_publisher:
                logger.error("[FaceRecoManager] ❌ ZMQ Publisher 未初始化")
                if self.tts_client:
                    self.tts_client.speak("系统错误，请重试")
                return False

            # 构造 ZMQ 命令数据
            command_payload = {
                "method": "current",  # 使用当前检测到的人脸
                "name": name,
                "metadata": {
                    'bbox': data.get('bbox', {}),
                    'tracker_id': data.get('tracker_id'),
                    'face_id': data.get('face_id'),
                    'register_source': 'voice_command'
                },
                "request_id": f"register_{int(time.time() * 1000)}"
            }

            # 发送命令到 Vision Service
            logger.info(f"[FaceRecoManager] 📤 发送 ZMQ 命令: cmd.vision.face.register, name={name}")
            self.zmq_publisher.publish_command(
                topic="cmd.vision.face.register",
                data=command_payload
            )

            # TTS 反馈（事件处理在 _handle_face_registered_event）
            if self.tts_client:
                try:
                    self.tts_client.speak(f"正在注册{ name }的人脸，请稍候", play_now=True)
                except Exception:
                    pass

            logger.info(f"[FaceRecoManager] ✅ 已发送注册命令: {name}")
            return True

        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 注册人脸异常: {e}", exc_info=True)
            if self.tts_client:
                try:
                    self.tts_client.speak("注册失败，请重试")
                except Exception:
                    pass
            return False
            
            logger.info(f"[FaceRecoManager] 🔄 执行人脸注册: {name}")
            
            # ✅ 新架构：通过 ZMQ 命令发送给 Vision Service
            if not self.zmq_publisher:
                logger.error("[FaceRecoManager] ❌ ZMQ Publisher 未初始化")
                if self.tts_client:
                    self.tts_client.speak("系统错误，请重试")
                return False
            
            # 构造 ZMQ 命令数据
            command_payload = {
                "method": "current",  # 使用当前检测到的人脸
                "name": name,
                "metadata": {
                    'bbox': data.get('bbox', {}),
                    'tracker_id': data.get('tracker_id'),
                    'face_id': data.get('face_id'),
                    'register_source': 'voice_command'
                },
                "request_id": f"register_{int(time.time() * 1000)}"
            }
            
            # 发送命令到 Vision Service
            logger.info(f"[FaceRecoManager] 📤 发送 ZMQ 命令: cmd.vision.face.register, name={name}")
            self.zmq_publisher.publish_command(
                topic="cmd.vision.face.register",
                data=command_payload
            )
            
            # TTS 反馈（事件处理在 _handle_face_registered_event）
            if self.tts_client:
                self.tts_client.speak(f"正在注册人脸，请稍候")
            
            logger.info(f"[FaceRecoManager] ✅ 已发送注册命令: {name}")
            return True
                
        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 注册人脸异常: {e}", exc_info=True)
            if self.tts_client:
                self.tts_client.speak("注册失败，请重试")
            return False
    
    def handle_update_face_command(self, command_data: Optional[Dict] = None) -> bool:
        """
        处理更新人脸命令（cmd_UpdateFace） - 同步版本
        
        Args:
            command_data: 命令数据（需包含 db_id 或 name, 以及 new_name）
            
        Returns:
            是否成功
        """
        logger.info("[FaceRecoManager] ✏️ 收到语音命令: cmd_UpdateFace (更新人脸)")
        
        if not self.confirmation_handler:
            if self.tts_client:
                self.tts_client.speak("人脸管理功能未启用")
            return False
        
        command_data = command_data or {}
        
        # 提取更新信息
        update_data = {
            'db_id': command_data.get('db_id'),
            'old_name': command_data.get('name'),
            'new_name': command_data.get('new_name', '已更新')
        }
        
        # ✅ 新架构：不再需要预先查找 db_id，Vision Service 支持按 name 查询
        if not update_data['old_name'] and not update_data['db_id']:
            logger.warning("[FaceRecoManager] ⚠️ 未指定要更新的人脸")
            if self.tts_client:
                self.tts_client.speak("请指定要更新的人脸")
            return False
        
        # 请求确认（同步调用）
        prompt = f"准备更新人脸信息，请说'确认'继续"
        timeout = 30
        
        success = self.confirmation_handler.request_confirmation(
            operation_type='update_face',
            operation_data=update_data,
            prompt=prompt,
            callback=self._execute_update_face,
            timeout=timeout
        )
        
        return success
    
    def _execute_update_face(self, operation: Dict[str, Any]) -> bool:
        """执行更新人脸 - 通过 ZMQ 命令"""
        try:
            data = operation['data']
            db_id = data.get('db_id')
            old_name = data.get('old_name')
            new_name = data['new_name']
            
            logger.info(f"[FaceRecoManager] 🔄 执行人脸更新: old_name={old_name}, new_name={new_name}")
            
            # ✅ 新架构：通过 ZMQ 命令发送给 Vision Service
            if not self.zmq_publisher:
                logger.error("[FaceRecoManager] ❌ ZMQ Publisher 未初始化")
                if self.tts_client:
                    self.tts_client.speak("系统错误，请重试")
                return False
            
            # 构造 ZMQ 命令数据（支持按 name 查询）
            command_payload = {
                "name": old_name,  # 旧名称（用于查找）
                "updates": {
                    "name": new_name  # 新名称
                },
                "request_id": f"update_{int(time.time() * 1000)}"
            }
            
            # 发送命令到 Vision Service
            logger.info(f"[FaceRecoManager] 📤 发送 ZMQ 命令: cmd.vision.face.update")
            self.zmq_publisher.publish_command(
                topic="cmd.vision.face.update",
                data=command_payload
            )
            
            # TTS 反馈
            if self.tts_client:
                self.tts_client.speak(f"正在更新人脸信息")
            
            logger.info(f"[FaceRecoManager] ✅ 已发送更新命令")
            return True
                
        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 更新人脸异常: {e}", exc_info=True)
            if self.tts_client:
                self.tts_client.speak("更新失败，请重试")
            return False
    
    def handle_delete_face_command(self, command_data: Optional[Dict] = None) -> bool:
        """
        处理删除人脸命令（cmd_DeleteFace） - 同步版本
        
        Args:
            command_data: 命令数据（需包含 db_id 或 name）
            
        Returns:
            是否成功
        """
        logger.info("[FaceRecoManager] 🗑️ 收到语音命令: cmd_DeleteFace (删除人脸)")
        
        if not self.confirmation_handler:
            if self.tts_client:
                self.tts_client.speak("人脸管理功能未启用")
            return False
        
        command_data = command_data or {}
        
        # 提取删除信息
        delete_data = {
            'name': command_data.get('name')
        }
        
        # ✅ 新架构：Vision Service 支持按 name 查询和删除
        if not delete_data['name']:
            logger.warning("[FaceRecoManager] ⚠️ 未指定要删除的人脸")
            if self.tts_client:
                self.tts_client.speak("请指定要删除的人脸")
            return False
        
        # 请求确认（删除操作需要强警告，同步调用）
        prompt = "警告！即将删除人脸数据，此操作不可恢复，请说'确认'继续"
        timeout = 30
        
        success = self.confirmation_handler.request_confirmation(
            operation_type='delete_face',
            operation_data=delete_data,
            prompt=prompt,
            callback=self._execute_delete_face,
            timeout=timeout
        )
        
        return success
    
    def _execute_delete_face(self, operation: Dict[str, Any]) -> bool:
        """执行删除人脸 - 通过 ZMQ 命令"""
        try:
            data = operation['data']
            name = data['name']
            
            logger.info(f"[FaceRecoManager] 🔄 执行人脸删除: name={name}")
            
            # ✅ 新架构：通过 ZMQ 命令发送给 Vision Service
            if not self.zmq_publisher:
                logger.error("[FaceRecoManager] ❌ ZMQ Publisher 未初始化")
                if self.tts_client:
                    self.tts_client.speak("系统错误，请重试")
                return False
            
            # 构造 ZMQ 命令数据
            command_payload = {
                "name": name,
                "request_id": f"delete_{int(time.time() * 1000)}"
            }
            
            # 发送命令到 Vision Service
            logger.info(f"[FaceRecoManager] 📤 发送 ZMQ 命令: cmd.vision.face.delete")
            self.zmq_publisher.publish_command(
                topic="cmd.vision.face.delete",
                data=command_payload
            )
            
            # TTS 反馈
            if self.tts_client:
                self.tts_client.speak("正在删除人脸数据")
            
            logger.info(f"[FaceRecoManager] ✅ 已发送删除命令")
            return True
                
        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 删除人脸异常: {e}", exc_info=True)
            if self.tts_client:
                self.tts_client.speak("删除失败，请重试")
            return False
    
    def handle_query_face_command(self, command_data: Optional[Dict] = None) -> bool:
        """
        处理查询人脸命令（cmd_QueryFace） - 通过 ZMQ 命令查询
        
        直接发送查询命令，结果通过事件返回（无需确认）
        
        Args:
            command_data: 命令数据（可选，用于指定查询条件）
            
        Returns:
            是否成功
        """
        logger.info("[FaceRecoManager] 🔍 收到语音命令: cmd_QueryFace (查询人脸)")
        
        try:
            # ✅ 新架构：通过 ZMQ 发送查询请求到 Vision Service
            # Vision Service 将返回人脸列表，通过订阅的 query response 处理
            
            # 注意：这里暂时使用同步查询（后续可改为异步订阅事件）
            # 由于 Vision Service 的 query 接口在 query_socket（REP/REQ模式）
            # 而不是通过 ZMQ pub/sub，所以暂时使用简化的 TTS 反馈
            
            if self.tts_client:
                self.tts_client.speak("正在查询人脸数据库，请稍候")
            
            logger.info("[FaceRecoManager] � 查询命令已发送（等待 Vision Service 响应）")
            
            # TODO: 添加 ZMQ REQ/REP 查询客户端
            # 或者通过订阅 event.vision.face.query.result 事件
            
            return True
            
        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 查询人脸异常: {e}", exc_info=True)
            if self.tts_client:
                self.tts_client.speak("查询失败")
            return False
    
    # ========== 状态检查 ==========
    
    def _get_relation_from_name(self, name: str) -> FaceRelation:
        """
        根据姓名获取关系类型
        
        Args:
            name: 人脸姓名
            
        Returns:
            FaceRelation 枚举
        """
        relation_str = None
        candidates = []
        normalized_name = (name or '').strip()
        if normalized_name:
            candidates.append(normalized_name)
            lower_name = normalized_name.lower()
            if lower_name != normalized_name:
                candidates.append(lower_name)

            trimmed_name = normalized_name
            while '_' in trimmed_name:
                trimmed_name = trimmed_name.rsplit('_', 1)[0].strip()
                if trimmed_name and trimmed_name not in candidates:
                    candidates.append(trimmed_name)
                lower_trimmed = trimmed_name.lower()
                if trimmed_name and lower_trimmed not in candidates:
                    candidates.append(lower_trimmed)

        for candidate in candidates:
            relation_str = self.name_to_relation.get(candidate)
            if relation_str:
                if candidate != normalized_name:
                    logger.info(f"[FaceRecoManager] 姓名 '{normalized_name}' 未直接命中，回退匹配 '{candidate}' -> {relation_str}")
                break
        
        # 如果找不到映射，视为已注册但未分配关系标签
        if not relation_str:
            logger.debug(f"[FaceRecoManager] 姓名 '{name}' 未找到关系映射，使用 UNKNOWN")
            return FaceRelation.UNKNOWN
        
        # 转换为枚举
        try:
            return FaceRelation(relation_str)
        except ValueError:
            logger.warning(f"[FaceRecoManager] 无效的关系类型 '{relation_str}'，使用 STRANGER")
            return FaceRelation.STRANGER
    
    def _should_process_events(self) -> bool:
        """检查是否应该处理事件"""
        if not self.enabled:
            return False
            
        # 检查当前状态
        if self.state_provider:
            try:
                current_state = self.state_provider()
                if hasattr(current_state, 'value'):
                    current_state = current_state.value
                if str(current_state).lower() in self.ignore_states:
                    return False
            except Exception:
                pass
                
        return True
    
    # ========== 事件处理 ==========
    
    def handle_event(self, event: Dict[str, Any]) -> bool:
        """
        处理视觉服务事件
        
        Args:
            event: 事件数据（可以是 DolyEvent 对象或字典）
            
        Returns:
            是否成功处理
        """
        if not self._should_process_events():
            return False
        
        # 处理 DolyEvent 对象 vs 字典
        if hasattr(event, 'data'):
            # DolyEvent 对象
            event_data = event.data
            if isinstance(event_data, dict) and 'data' in event_data:
                # 嵌套的 data 结构
                event_data = event_data['data']
        else:
            # 字典
            event_data = event
        
        event_type = event_data.get('event', '')
        # logger.info(f"[FaceRecoManager] 📨 接收到事件: type={event_type}, data={event_data}")
        # logger.info(f"------> 1 [FaceRecoManager] 🔍 话题：{event.topic}  处理人脸识别事件: {event_data}")
        if event.topic == "event.vision.face.recognized":
            logger.info(f"--------> 2 [FaceRecoManager] 🔍 话题：{event.topic}  处理人脸识别事件: {event_data}")
        try:
            if event_type == 'face_snapshot' or event_type == 'face_detected':
                return self._handle_face_snapshot(event_data)
            elif event_type == 'face_recognized':
                # 人脸识别，只处理第一次的事件，也就是在topic为event.vision.face.recognized的情况！
                # 判断topic为event.vision.face.recognized
                if event.topic == "event.vision.face.recognized":
                    logger.info(f"--------> 3 [FaceRecoManager] 🔍 处理人脸识别事件: {event_data}")
                    return self._handle_face_recognized(event_data)
                else:
                    logger.info(f"[FaceRecoManager] 🔍 已经处理过的人脸识别事件，跳过！")
            elif event_type == 'face_new':
                return self._handle_new_face(event_data)
            elif event_type == 'face_lost':
                return self._handle_face_lost(event_data)
            # Vision Service 发布的注册/更新/删除事件
            elif event.topic == 'event.vision.face.registered' or event.topic == 'event.vision.face.updated' or event.topic == 'event.vision.face.deleted':
                try:
                    # 统一处理这些事件，通过 topic 分发到具体处理函数
                    if event.topic == 'event.vision.face.registered':
                        return self._handle_face_registered_event(event_data)
                    elif event.topic == 'event.vision.face.updated':
                        return self._handle_face_updated_event(event_data)
                    elif event.topic == 'event.vision.face.deleted':
                        return self._handle_face_deleted_event(event_data)
                except Exception as e:
                    logger.error(f"[FaceRecoManager] 处理 face event 出错: {e}", exc_info=True)
                    return False
            # ★★★ 新增：拍照/录像结果事件 ★★★
            elif event.topic == 'event.vision.capture.complete':
                return self._handle_capture_complete_event(event_data)
            elif event.topic == 'event.vision.capture.started':
                return self._handle_capture_started_event(event_data)
            else:
                # 忽略状态事件等
                if event_type and 'state' not in event_type:
                    logger.debug(f"[FaceRecoManager] 未处理的事件类型: {event_type}")
                return False
        except Exception as e:
            logger.error(f"[FaceRecoManager] 处理事件异常: {e}", exc_info=True)
            return False
    
    def _handle_face_snapshot(self, event: Dict[str, Any]) -> bool:
        """处理人脸快照事件"""
        faces = event.get('faces', [])
        primary = event.get('primary', {})
        
        with self._lock:
            # 更新当前跟踪的人脸
            seen_ids = set()
            for face_data in faces:
                tracker_id = face_data.get('id')
                if tracker_id is None:
                    continue
                    
                seen_ids.add(tracker_id)
                
                if tracker_id not in self.current_faces:
                    # 新的跟踪目标
                    self.current_faces[tracker_id] = TrackedFace(
                        tracker_id=tracker_id,
                        first_seen_ms=int(time.time() * 1000)
                    )
                
                # 更新位置信息
                face = self.current_faces[tracker_id]
                face.bbox = face_data.get('bbox', {})
                face.normalized = face_data.get('normalized', {})
                face.last_seen_ms = int(time.time() * 1000)
                
                # ★★★ 眼神跟随：更新眼睛注视位置 ★★★
                if self.gaze_tracker and face.bbox:
                    # bbox 格式: {'x': 100, 'y': 150, 'w': 80, 'h': 100}
                    bbox_list = [
                        face.bbox.get('x', 0),
                        face.bbox.get('y', 0),
                        face.bbox.get('w', 0),
                        face.bbox.get('h', 0)
                    ]
                    gaze = self.gaze_tracker.bbox_to_gaze(bbox_list)
                    
                    if gaze and self.eye_client:
                        x, y = gaze
                        # 获取配置的移动时长
                        gaze_config = self.config.get('face_recognition', {}).get('gaze_tracking', {})
                        duration = gaze_config.get('duration', 300)
                        
                        # 调用 eyeEngine 设置眼神位置
                        try:
                            self.eye_client.set_gaze(x, y, duration=duration)
                            logger.debug(f"[FaceRecoManager] 👀 眼神跟随: ({x:.2f}, {y:.2f})")
                        except Exception as e:
                            logger.error(f"[FaceRecoManager] 眼神跟随失败: {e}")
                face.confidence = face_data.get('confidence', 0.0)
                face.liveness = face_data.get('liveness', False)
                face.angle = face_data.get('angle', 0.0)
                
                # 更新姓名信息（仅更新，不触发行为）
                name = face_data.get('name', '')
                if name and name != 'unknown':
                    face.name = name
                    face.recognized = True
                    # 确定关系
                    face.relation = self._get_relation_from_name(face.name)
                
                # ⚠️ 注意: event.vision.face 是每帧都发送的检测事件
                # 不应该在这里触发识别行为，应该由 event.vision.face.recognized 事件触发
            
            # 更新主要人脸
            if primary:
                self.primary_face_id = primary.get('id')
                
                # 眼神跟随
                if self.gaze_config.get('enabled', True):
                    self._update_gaze_follow(primary)
            
            # ★★★ 保存最近检测到的人脸（用于注册）★★★
            if faces:
                # 保存第一个检测到的人脸
                self.last_detected_face = self.current_faces.get(faces[0].get('id'))
                logger.debug(f"[FaceRecoManager] 💾 保存最近检测到的人脸: tracker_id={self.last_detected_face.tracker_id if self.last_detected_face else None}")
                
                # ★★★ 新增：如果有待处理的注册命令，现在执行 ★★★
                if self.pending_register_command and self.last_detected_face:
                    logger.info("[FaceRecoManager] 🎯 检测到人脸，执行待处理的注册命令")
                    self.pending_register_command = False
                    
                    # 取消超时计时器
                    if self.register_timeout_timer:
                        self.register_timeout_timer.cancel()
                        self.register_timeout_timer = None
                    
                    # 执行注册
                    self._execute_register_face({
                        'data': {
                            'name': self.register_name or '新朋友',
                            'bbox': self.last_detected_face.bbox,
                            'tracker_id': self.last_detected_face.tracker_id,
                            'face_id': self.last_detected_face.face_id
                        }
                    })
        
        return True
    
    def _handle_face_recognized(self, event: Dict[str, Any]) -> bool:
        """处理人脸识别成功事件"""
        tracker_id = event.get('tracker_id', event.get('id'))
        face_id = event.get('face_id', '')
        name = event.get('name', 'unknown')
        raw_confidence = event.get('confidence', 0.0)
        liveness = event.get('liveness', False)
        metadata = event.get('metadata', {})
        match_score = metadata.get('match_score', event.get('match_score'))
        liveness_confidence = metadata.get('liveness_confidence', raw_confidence)

        if not name or name == 'unknown':
            logger.debug(f"[FaceRecoManager] 跳过无效识别事件: tracker_id={tracker_id}, name={name}")
            return False

        if match_score is not None:
            try:
                match_score = float(match_score)
            except (TypeError, ValueError):
                match_score = None

        if match_score is not None and match_score < self.recognition_threshold:
            logger.debug(
                f"[FaceRecoManager] 识别匹配分数不足: {name} ({match_score:.5f}, 阈值: {self.recognition_threshold})"
            )

            if self.force_greet_next_recognition:
                temp_face = self.current_faces.get(tracker_id) or TrackedFace(
                    tracker_id=tracker_id,
                    first_seen_ms=int(time.time() * 1000),
                    last_seen_ms=int(time.time() * 1000),
                )
                temp_face.name = name
                logger.info("[FaceRecoManager] 🤷 cmd_ActWhoami检测到低分匹配，按陌生人处理")
                self._trigger_stranger_behavior(temp_face)
                self.force_greet_next_recognition = False

                if self.vision_mode_manager:
                    def switch_to_idle_and_unlock():
                        self.vision_mode_manager.set_mode('IDLE')
                        if self.daemon and hasattr(self.daemon, 'unlock_command'):
                            self.daemon.unlock_command('cmd_ActWhoami')
                    threading.Timer(2.0, switch_to_idle_and_unlock).start()

            return False

        recognition_key = str(face_id or name or tracker_id)
        now = time.time()
        history = self.recognition_history.get(recognition_key)
        if history is None:
            history = {
                'last_seen': 0,
                'last_liveness': None,
                'greeted': False,
                'last_recognition': 0,
            }
            self.recognition_history[recognition_key] = history

        if not self.force_greet_next_recognition and self.recognition_cooldown > 0:
            last_recognition = float(history.get('last_recognition', 0) or 0)
            elapsed = now - last_recognition
            if last_recognition > 0 and elapsed < self.recognition_cooldown:
                logger.debug(
                    f"[FaceRecoManager] 识别冷却中: {recognition_key} (剩余 {self.recognition_cooldown - elapsed:.1f}s)"
                )
                return False
        
        with self._lock:
            # 更新或创建跟踪数据
            if tracker_id in self.current_faces:
                face = self.current_faces[tracker_id]
            else:
                face = TrackedFace(
                    tracker_id=tracker_id,
                    first_seen_ms=int(time.time() * 1000),
                    last_seen_ms=int(time.time() * 1000)
                )
                self.current_faces[tracker_id] = face

            
            face.face_id = face_id
            face.name = name
            face.confidence = match_score if match_score is not None else raw_confidence
            face.liveness = liveness
            face.recognized = True
            
            # 根据姓名确定关系类型（从配置映射表）
            face.relation = self._get_relation_from_name(name)
            logger.info(f"[FaceRecoManager] 🔍 姓名映射调试: name='{name}', name_to_relation={self.name_to_relation}, relation={face.relation.value}")

            history['last_recognition'] = now
            history['last_seen'] = now
        
        # 检查活体：如果 liveness=False（伪人脸），触发 fake 行为
        if not liveness:
            logger.warning(f"[FaceRecoManager] ⚠️  检测到伪人脸: {name} (liveness=False)")
            
            # 更新识别历史（记录伪人脸状态）
            if name not in self.recognition_history:
                self.recognition_history[name] = {
                    'last_seen': time.time(),
                    'last_liveness': False,
                    'greeted': False
                }
            else:
                self.recognition_history[name]['last_liveness'] = False
                self.recognition_history[name]['last_seen'] = time.time()
            
            logger.info(f"[FaceRecoManager] 伪人脸处理: auto_greet={self.auto_greet}, face.greeted={face.greeted}")
            if self.auto_greet and not face.greeted:
                logger.info(f"[FaceRecoManager] 🎬 触发伪人脸行为: {name}")
                self._trigger_fake_face_behavior(face)
            else:
                logger.debug(f"[FaceRecoManager] 跳过伪人脸行为: auto_greet={self.auto_greet}, greeted={face.greeted}")
            face.greeted = True  # 标记为已处理，避免重复触发
        else:
            # 真人脸：触发打招呼
            logger.debug(f"[FaceRecoManager] 真人脸：liveness=True")
            if self.auto_greet and not face.greeted:
                # 检查是否使用强制打招呼（用于cmd_ActWhoami）
                self._trigger_greeting(face, force_greet=self.force_greet_next_recognition)
            else:
                logger.debug(f"[FaceRecoManager] 跳过打招呼: auto_greet={self.auto_greet}, greeted={face.greeted}")
        
        # 回调
        if self.on_face_recognized:
            try:
                self.on_face_recognized(face)
            except Exception as e:
                logger.error(f"[FaceRecoManager] 识别回调异常: {e}")
        
        logger.info(
            f"[FaceRecoManager] 识别到: {name} "
            f"(match_score={match_score if match_score is not None else 'n/a'}, "
            f"liveness_score={liveness_confidence:.2f}, 关系={face.relation.value}, 活体={liveness})"
        )
        
        # 自动回 IDLE 模式（如果启用）
        if self.vision_mode_manager:
            mode_config = self.config.get('mode', {})
            auto_switch = mode_config.get('auto_switch_idle', True)
            
            if auto_switch:
                # 取消超时计时器（避免重复切换）
                self.vision_mode_manager.cancel_timeout()
                
                # 延迟回到 IDLE（给问候动画时间完成）
                def switch_to_idle_and_unlock():
                    # ★★★ 重置强制打招呼标志 ★★★
                    self.force_greet_next_recognition = False
                    logger.debug("[FaceRecoManager] 重置强制打招呼标志")
                    
                    # ★★★ 清除已识别人脸的greeted标志，允许下一次识别再次打招呼 ★★★
                    # 这样当用户再次cmd_ActWhoami时，即使在冷却期内也能因为force_greet而打招呼
                    if recognition_key in self.recognition_history:
                        self.recognition_history[recognition_key]['greeted'] = False
                        logger.debug(f"[FaceRecoManager] 清除 {recognition_key} 的greeted标志")
                    face.greeted = False
                    
                    self.vision_mode_manager.set_mode('IDLE')
                    # ★★★ 解锁 cmd_ActWhoami 命令，允许重新触发 ★★★
                    if self.daemon and hasattr(self.daemon, 'unlock_command'):
                        self.daemon.unlock_command('cmd_ActWhoami')
                
                threading.Timer(2.0, switch_to_idle_and_unlock).start()
                logger.debug("[FaceRecoManager] 识别完成，2秒后自动回 IDLE 并解锁命令")
        
        return True

    # ========== Vision Face DB 事件处理 ==========
    def _handle_face_registered_event(self, event: Dict[str, Any]) -> bool:
        """处理 Vision Service 返回的 face.registered 事件
        事件 data 预期包含: success, face_id, name, request_id, message
        """
        try:
            data = event.get('data', event)
            success = data.get('success', False)
            name = data.get('name', '')
            face_id = data.get('face_id', '')
            message = data.get('message', '')

            logger.info(f"[FaceRecoManager] 📥 face.registered: name={name}, id={face_id}, ok={success}")

            if success:
                # 可选：将结果同步到 known_faces（只保留最小信息）
                with self._lock:
                    if face_id and name:
                        profile = FaceProfile(face_id=face_id, name=name)
                        self.known_faces[name] = profile

                # TTS 反馈
                if self.tts_client:
                    self.tts_client.speak(f"已为 {name} 完成人脸注册。")

            else:
                logger.warning(f"[FaceRecoManager] face.registered failed: {message}")
                if self.tts_client:
                    self.tts_client.speak(f"人脸注册失败：{message}")

            return True
        except Exception as e:
            logger.error(f"[FaceRecoManager] 处理 face.registered 异常: {e}", exc_info=True)
            return False

    def _handle_face_updated_event(self, event: Dict[str, Any]) -> bool:
        """处理 Vision Service 返回的 face.updated 事件"""
        try:
            data = event.get('data', event)
            success = data.get('success', False)
            face_id = data.get('face_id', '')
            updated_fields = data.get('updated_fields', [])
            name = data.get('name', '')
            logger.info(f"[FaceRecoManager] 📥 face.updated: id={face_id}, name={name}, ok={success}")
            if success and name:
                with self._lock:
                    # 更新已知人脸映射（按 name 为 key）
                    profile = self.known_faces.get(name)
                    if not profile and face_id:
                        profile = FaceProfile(face_id=face_id, name=name)
                        self.known_faces[name] = profile

                if self.tts_client:
                    self.tts_client.speak(f"已更新 {name} 的人脸信息。")
            else:
                if self.tts_client:
                    self.tts_client.speak("人脸更新失败。")

            return True
        except Exception as e:
            logger.error(f"[FaceRecoManager] 处理 face.updated 异常: {e}", exc_info=True)
            return False

    def _handle_face_deleted_event(self, event: Dict[str, Any]) -> bool:
        """处理 Vision Service 返回的 face.deleted 事件"""
        try:
            data = event.get('data', event)
            success = data.get('success', False)
            face_id = data.get('face_id', '')
            name = data.get('name', '')

            logger.info(f"[FaceRecoManager] 📥 face.deleted: id={face_id}, name={name}, ok={success}")
            if success:
                with self._lock:
                    # 从 known_faces 中移除（按 name 键）
                    if name and name in self.known_faces:
                        del self.known_faces[name]

                if self.tts_client:
                    self.tts_client.speak(f"已删除 {name} 的人脸记录。")
            else:
                if self.tts_client:
                    self.tts_client.speak("人脸删除失败。")

            return True
        except Exception as e:
            logger.error(f"[FaceRecoManager] 处理 face.deleted 异常: {e}", exc_info=True)
            return False
    
    def _handle_new_face(self, event: Dict[str, Any]) -> bool:
        """处理新人脸出现事件"""
        tracker_id = event.get('tracker_id', event.get('id'))
        detection_count = event.get('detection_count', 0)
        liveness = event.get('liveness', False)
        now_ms = int(time.time() * 1000)
        cooldown_ms = int(float(self.new_face_config.get('cooldown_seconds', 8)) * 1000)

        if tracker_id is None:
            logger.warning(f"[FaceRecoManager] 新人脸事件缺少 tracker_id: {event}")
            return False
        
        if not self.new_face_config.get('enabled', True):
            return False
        
        # 活体检测检查
        if self.liveness_required and not liveness:
            return False
        
        with self._lock:
            # 创建或更新跟踪记录
            if tracker_id not in self.current_faces:
                self.current_faces[tracker_id] = TrackedFace(
                    tracker_id=tracker_id,
                    first_seen_ms=now_ms,
                    last_seen_ms=now_ms
                )
            
            face = self.current_faces[tracker_id]
            face.last_seen_ms = now_ms
            face.liveness = liveness
            face.bbox = event.get('bbox', {})
            face.recognized = False
            face.relation = FaceRelation.STRANGER
            self.last_detected_face = face

            should_trigger = True
            if cooldown_ms > 0 and self._last_new_face_prompt_ms > 0:
                elapsed_ms = now_ms - self._last_new_face_prompt_ms
                if elapsed_ms < cooldown_ms:
                    should_trigger = False
                    logger.info(
                        f"[FaceRecoManager] 新人脸提示冷却中: tracker_id={tracker_id}, 剩余={(cooldown_ms - elapsed_ms) / 1000:.1f}s"
                    )

            if should_trigger:
                self._last_new_face_prompt_ms = now_ms

        if not should_trigger:
            return True
        
        # 触发新人脸行为
        self._trigger_new_face_behavior(face)
        
        # 回调
        if self.on_new_face:
            try:
                self.on_new_face(face)
            except Exception as e:
                logger.error(f"[FaceRecoManager] 新人脸回调异常: {e}")
        
        logger.info(f"[FaceRecoManager] 新人脸出现: tracker_id={tracker_id}, 检测次数={detection_count}")
        return True
    
    def _handle_face_lost(self, event: Dict[str, Any]) -> bool:
        """处理人脸消失事件"""
        tracker_id = event.get('tracker_id', event.get('id'))
        logger.info(f"[FaceRecoManager] 🔴 接收到人脸消失事件: tracker_id={tracker_id}, event={event}")
        
        with self._lock:
            if tracker_id not in self.current_faces:
                logger.warning(f"[FaceRecoManager] 人脸消失：跟踪器 {tracker_id} 未找到")
                return False
            
            face = self.current_faces[tracker_id]
            logger.info(f"[FaceRecoManager] 人脸消失：{face.name or '未知人脸'} (id={tracker_id})")
            
            # 触发消失行为
            if face.recognized:
                logger.info(f"[FaceRecoManager] 触发消失行为")
                self._trigger_face_lost_behavior(face)
            
            # 回调
            if self.on_face_lost:
                try:
                    logger.info(f"[FaceRecoManager] 调用 on_face_lost 回调")
                    self.on_face_lost(face)
                except Exception as e:
                    logger.error(f"[FaceRecoManager] 消失回调异常: {e}")
            
            # 移除跟踪记录
            logger.info(f"[FaceRecoManager] 删除人脸记录: {tracker_id}")
            del self.current_faces[tracker_id]
            
            # 更新主要人脸
            if self.primary_face_id == tracker_id:
                logger.info(f"[FaceRecoManager] 主人脸已消失，重新选择")
                self.primary_face_id = None
                if self.current_faces:
                    self.primary_face_id = next(iter(self.current_faces.keys()))
                    logger.info(f"[FaceRecoManager] 新主人脸: {self.primary_face_id}")
                else:
                    # ★★★ 所有人脸消失：重置眼神到中心 ★★★
                    if self.gaze_tracker:
                        self.gaze_tracker.reset()
                        if self.eye_client:
                            try:
                                self.eye_client.set_gaze(0.0, 0.0, duration=500)
                                logger.info("[FaceRecoManager] 👀 眼神已重置到中心位置")
                            except Exception as e:
                                logger.error(f"[FaceRecoManager] 眼神重置失败: {e}")
                    
                    if self.on_all_faces_lost:
                        logger.info(f"[FaceRecoManager] 所有人脸已消失，调用 on_all_faces_lost 回调")
                        try:
                            self.on_all_faces_lost()
                        except Exception as e:
                            logger.error(f"[FaceRecoManager] 全部消失回调异常: {e}")
        
        logger.info(f"[FaceRecoManager] 人脸消失: {face.name} (tracker_id={tracker_id})")
        return True
    
    # ========== 行为触发 ==========
    
    def _trigger_greeting(self, face: TrackedFace, force_greet: bool = False) -> None:
        """
        触发打招呼行为（含防重复逻辑和活体变化检测）
        
        Args:
            face: 被识别的人脸
            force_greet: 是否强制打招呼（绕过冷却时间限制）
                        用于用户显式触发cmd_ActWhoami的场景
        """
        now = time.time()
        name = face.name
        history_key = str(face.face_id or name)
        liveness = face.liveness
        
        # 获取或初始化识别历史
        if history_key not in self.recognition_history:
            self.recognition_history[history_key] = {
                'last_seen': 0,
                'last_liveness': None,
                'greeted': False,
                'last_recognition': 0
            }
        
        history = self.recognition_history[history_key]
        
        # **活体变化检测**：从伪人脸变为真人脸
        if (self.liveness_change_alert and 
            history['last_liveness'] is False and 
            liveness is True):
            logger.warning(f"[FaceRecoManager] ⚠️  检测到活体状态变化: {name} (假→真)")
            # 触发特殊提醒而非问候
            self._trigger_liveness_change_alert(face)
            history['last_liveness'] = liveness
            history['last_seen'] = now
            return  # 活体变化提醒后不再执行普通问候
        
        # **冷却时间检查** (不适用于强制打招呼)
        if not force_greet:
            elapsed = now - history['last_seen']
            if elapsed < self.greet_cooldown and history['greeted']:
                logger.debug(f"[FaceRecoManager] 打招呼冷却中: {name} (剩余 {self.greet_cooldown - elapsed:.0f}s)")
                return
        else:
            logger.info(f"[FaceRecoManager] 💪 强制打招呼: {name} (绕过冷却时间限制)")
        
        # 获取行为配置
        relation_key = face.relation.value
        behavior = self.known_face_behaviors.get(relation_key, {})

        if face.relation == FaceRelation.UNKNOWN or not behavior:
            logger.debug(f"[FaceRecoManager] 通用打招呼: {name}")

            greeting_text = f"{name}，你好" if name else "你好"
            try:
                if self.tts_client:
                    self.tts_client.speak(greeting_text, play_now=True)
                    logger.info(f"[FaceRecoManager] 🎤 通用TTS: {greeting_text}")
                else:
                    logger.warning(f"[FaceRecoManager] TTS管理器未设置，跳过语音: {greeting_text}")
            except Exception as e:
                logger.error(f"[FaceRecoManager] 通用TTS失败: {e}", exc_info=True)

            face.greeted = True
            history['greeted'] = True
            history['last_seen'] = now
            history['last_liveness'] = liveness
            logger.info(f"[FaceRecoManager] 通用打招呼完成: {name}")
            return
        
        if not behavior:
            logger.debug(f"[FaceRecoManager] 未找到 {relation_key} 的行为配置")
            return
        
        priority = behavior.get('priority', 5)
        
        # 播放动画
        animation = behavior.get('greeting_animation')
        if animation and self.animation_manager:
            try:
                self.animation_manager.play_animation(animation, priority=priority)
                logger.debug(f"[FaceRecoManager] 播放动画: {animation}")
            except Exception as e:
                logger.error(f"[FaceRecoManager] 播放动画失败: {e}")
        
        # LED 效果
        led_effect = behavior.get('led_effect')
        if led_effect and self.led_controller:
            try:
                # TODO: 调用 LED 控制器
                logger.debug(f"[FaceRecoManager] 触发 LED: {led_effect}")
            except Exception as e:
                logger.error(f"[FaceRecoManager] LED 效果失败: {e}")
        
        # TTS 问候语
        tts_template = behavior.get('greeting_tts')
        if tts_template:
            greeting_text = tts_template.format(name=face.name)
            try:
                # 使用TTS管理器合成语音（支持情绪化）
                if self.tts_client:
                    # tts_client 现在是 TTSManager 实例
                    self.tts_client.speak(greeting_text, play_now=True)
                    logger.info(f"[FaceRecoManager] 🎤 TTS播放: {greeting_text}")
                else:
                    logger.warning(f"[FaceRecoManager] TTS管理器未设置，跳过语音: {greeting_text}")
            except Exception as e:
                logger.error(f"[FaceRecoManager] TTS 失败: {e}", exc_info=True)
        
        # 更新识别历史
        face.greeted = True
        history['greeted'] = True
        history['last_seen'] = now
        history['last_liveness'] = liveness
        
        logger.info(f"[FaceRecoManager] 打招呼完成: {name} ({relation_key})")
    
    def _trigger_stranger_behavior(self, face: TrackedFace) -> None:
        """触发陌生人/未识别行为（用于 cmd_ActWhoami 场景）"""
        logger.info("[FaceRecoManager] 🤷 触发陌生人行为")
        
        # 播放好奇动画
        if self.animation_manager:
            try:
                self.animation_manager.play_animation('CURIOUS.curious_look', priority=7)
                logger.info("[FaceRecoManager] 播放陌生人动画: CURIOUS.curious_look")
            except Exception as e:
                logger.error(f"[FaceRecoManager] 播放陌生人动画失败: {e}")
        
        # TTS 提示
        if self.tts_client:
            try:
                stranger_text = "我还不认识你，你是谁呀？要不要注册一下？"
                self.tts_client.speak(stranger_text, play_now=True)
                logger.info(f"[FaceRecoManager] 🎤 陌生人TTS: {stranger_text}")
            except Exception as e:
                logger.error(f"[FaceRecoManager] 陌生人TTS失败: {e}")
    
    def _trigger_liveness_change_alert(self, face: TrackedFace) -> None:
        """触发活体状态变化提醒（伪人脸→真人脸）"""
        logger.warning(f"[FaceRecoManager] 🚨 活体状态变化: {face.name} (假→真)")
        
        # 播放特殊提醒动画（使用疑惑表情）
        alert_animation = "CURIOUS.curious_look"
        if self.animation_manager:
            try:
                self.animation_manager.play_animation(alert_animation, priority=9)  # 高优先级
                logger.info(f"[FaceRecoManager] 播放活体变化提醒动画: {alert_animation}")
            except Exception as e:
                logger.error(f"[FaceRecoManager] 播放提醒动画失败: {e}")
        
        # 语音提醒
        alert_text = f"咦，{face.name}，你刚才是不是在玩照片？现在才是真人呢！"
        if self.tts_client:
            try:
                self.tts_client.speak(alert_text, play_now=True)
                logger.info(f"[FaceRecoManager] 🎤 活体变化提醒TTS: {alert_text}")
            except Exception as e:
                logger.error(f"[FaceRecoManager] 活体变化提醒TTS失败: {e}")
    
    def _trigger_fake_face_behavior(self, face: TrackedFace) -> None:
        """触发伪人脸行为（当 liveness=False 时）"""
        relation_key = face.relation.value
        behavior = self.known_face_behaviors.get(relation_key, {})
        
        if not behavior:
            logger.debug(f"[FaceRecoManager] 未找到 {relation_key} 的行为配置")
            return
        
        # 获取 fake 子配置
        fake_behavior = behavior.get('fake', {})
        if not fake_behavior:
            logger.debug(f"[FaceRecoManager] {relation_key} 未配置 fake 行为")
            return
        
        priority = fake_behavior.get('priority', 3)
        
        # 播放动画
        animation = fake_behavior.get('animation')
        if animation and self.animation_manager:
            try:
                self.animation_manager.play_animation(animation, priority=priority)
                logger.debug(f"[FaceRecoManager] 伪人脸动画: {animation}")
            except Exception as e:
                logger.error(f"[FaceRecoManager] 伪人脸动画失败: {e}")
        
        # LED 效果
        led_effect = fake_behavior.get('led_effect')
        if led_effect and self.led_controller:
            try:
                logger.debug(f"[FaceRecoManager] 伪人脸 LED: {led_effect}")
            except Exception as e:
                logger.error(f"[FaceRecoManager] 伪人脸 LED 失败: {e}")
        
        # TTS 问候语
        tts_template = fake_behavior.get('tts')
        if tts_template:
            fake_text = tts_template.format(name=face.name)
            try:
                if self.tts_client:
                    self.tts_client.speak(fake_text, play_now=True)
                    logger.info(f"[FaceRecoManager] 🎤 伪人脸TTS: {fake_text}")
                else:
                    logger.warning(f"[FaceRecoManager] TTS管理器未设置，跳过语音: {fake_text}")
            except Exception as e:
                logger.error(f"[FaceRecoManager] 伪人脸TTS失败: {e}", exc_info=True)
        
        logger.info(f"[FaceRecoManager] 触发伪人脸行为: {face.name} ({relation_key})")
    
    def _trigger_new_face_behavior(self, face: TrackedFace) -> None:
        """触发新人脸行为"""
        animation = self.new_face_config.get('animation', 'CURIOUS.curious_look')
        tts_text = self.new_face_config.get('tts')
        priority = self.new_face_config.get('priority', 6)
        
        if animation and self.animation_manager:
            try:
                self.animation_manager.play_animation(animation, priority=priority)
            except Exception as e:
                logger.error(f"[FaceRecoManager] 新人脸动画失败: {e}")
                
        if tts_text and getattr(self, 'tts_client', None):
            try:
                self.tts_client.speak(tts_text.format(name="新朋友"), play_now=True)
            except Exception as e:
                logger.error(f"[FaceRecoManager] 新人脸 TTS 失败: {e}")
        
        logger.debug(f"[FaceRecoManager] 触发新人脸行为: tracker_id={face.tracker_id}")
    
    def _trigger_face_lost_behavior(self, face: TrackedFace) -> None:
        """触发人脸消失行为"""
        delay = self.face_lost_config.get('delay_seconds', 2.0)
        animation = self.face_lost_config.get('animation')
        priority = self.face_lost_config.get('priority', 4)
        
        if not animation:
            return
        
        tracker_id = face.tracker_id
        
        def delayed_animation():
            time.sleep(delay)
            with self._lock:
                # 确认仍然不在
                if tracker_id not in self.current_faces:
                    if self.animation_manager:
                        try:
                            self.animation_manager.play_animation(animation, priority=priority)
                        except Exception as e:
                            logger.error(f"[FaceRecoManager] 消失动画失败: {e}")
        
        threading.Thread(target=delayed_animation, daemon=True).start()
    
    def _update_gaze_follow(self, primary_face: Dict[str, Any]) -> None:
        """更新眼神跟随"""
        if not self.eye_client:
            return
            
        normalized = primary_face.get('normalized', {})
        x = normalized.get('x', 0.5)
        y = normalized.get('y', 0.5)
        
        # 死区检查
        dead_zone = self.gaze_config.get('dead_zone', 0.05)
        if abs(x - 0.5) < dead_zone and abs(y - 0.5) < dead_zone:
            return
        
        try:
            # 发送眼神跟随命令
            if hasattr(self.eye_client, 'send_gaze_command'):
                self.eye_client.send_gaze_command(x, y)
            elif hasattr(self.eye_client, 'send_command'):
                self.eye_client.send_command('cmd.eye.gaze', {
                    'type': 'custom',
                    'x': x,
                    'y': y
                })
        except Exception as e:
            logger.debug(f"[FaceRecoManager] 眼神跟随失败: {e}")
    
    # ========== 人脸管理接口 ==========
    
    def enable_recognition(self, duration_seconds: int = 30) -> bool:
        """
        启用人脸识别功能（临时）
        
        Args:
            duration_seconds: 启用时长（秒）
            
        Returns:
            是否成功发送命令
        """
        if not self.zmq_publisher:
            logger.warning("[FaceRecoManager] 无法启用识别：ZMQ 发布器未设置")
            return False

        if self.vision_mode_manager:
            return self.vision_mode_manager.set_mode('FULL', timeout=duration_seconds)
        
        try:
            self.zmq_publisher.publish_command(
                topic="cmd.vision.mode",
                data={"target": "facereco", "mode": "FULL", "timeout": duration_seconds}
            )
            logger.info(f"[FaceRecoManager] ✅ 已启用人脸识别 ({duration_seconds}秒)")
            return True
        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 启用识别失败: {e}")
            return False
    
    def register_current_face(self, tracker_id: int, name: str, 
                               relation: str = "friend", 
                               metadata: Optional[Dict] = None) -> bool:
        """
        注册当前检测到的人脸
        
        Args:
            tracker_id: 跟踪 ID
            name: 人脸名称
            relation: 关系类型
            metadata: 元数据
            
        Returns:
            是否成功发送命令
        """
        if not self.zmq_publisher:
            logger.warning("[FaceRecoManager] ZMQ 发布器未设置")
            return False
        
        cmd = {
            "method": "current",
            "tracker_id": tracker_id,
            "name": name,
            "metadata": {
                "relation": relation,
                **(metadata or {})
            }
        }
        
        try:
            self.zmq_publisher.send("cmd.vision.face.register", cmd)
            logger.info(f"[FaceRecoManager] 发送注册命令: {name}")
            return True
        except Exception as e:
            logger.error(f"[FaceRecoManager] 注册命令发送失败: {e}")
            return False
    
    def get_current_faces(self) -> List[Dict[str, Any]]:
        """获取当前跟踪的人脸列表"""
        with self._lock:
            return [
                {
                    "tracker_id": face.tracker_id,
                    "face_id": face.face_id,
                    "name": face.name,
                    "recognized": face.recognized,
                    "confidence": face.confidence,
                    "liveness": face.liveness,
                    "relation": face.relation.value,
                    "bbox": face.bbox,
                    "normalized": face.normalized,
                    "greeted": face.greeted
                }
                for face in self.current_faces.values()
            ]
    
    def get_primary_face(self) -> Optional[Dict[str, Any]]:
        """获取主要（最大/最近）人脸"""
        with self._lock:
            if self.primary_face_id and self.primary_face_id in self.current_faces:
                face = self.current_faces[self.primary_face_id]
                return {
                    "tracker_id": face.tracker_id,
                    "face_id": face.face_id,
                    "name": face.name,
                    "recognized": face.recognized,
                    "normalized": face.normalized
                }
        return None
    
    def has_faces(self) -> bool:
        """是否有人脸正在被跟踪"""
        with self._lock:
            return len(self.current_faces) > 0
    
    def has_recognized_face(self) -> bool:
        """是否有已识别的人脸"""
        with self._lock:
            return any(face.recognized for face in self.current_faces.values())
    
    def find_face_by_name(self, name: str) -> Optional[TrackedFace]:
        """按名字查找人脸"""
        with self._lock:
            for face in self.current_faces.values():
                if face.name == name:
                    return face
        return None
    
    # ========== 生命周期 ==========
    
    def reload_config(self) -> bool:
        """重新加载配置"""
        self._load_config()
        logger.info("[FaceRecoManager] 配置已重新加载")
        return True
    
    def clear_tracking(self) -> None:
        """清空当前跟踪数据"""
        with self._lock:
            self.current_faces.clear()
            self.primary_face_id = None
        logger.info("[FaceRecoManager] 跟踪数据已清空")
        
    def clear_greet_history(self) -> None:
        """清空打招呼历史和识别历史"""
        with self._lock:
            self.last_greet_time.clear()  # 保留兼容性
            self.recognition_history.clear()
        logger.info("[FaceRecoManager] 打招呼历史和识别历史已清空")
        
    def enable(self, enabled: bool = True) -> None:
        """启用/禁用管理器"""
        self.enabled = enabled
        logger.info(f"[FaceRecoManager] {'启用' if enabled else '禁用'}")
    
    def get_status(self) -> Dict[str, Any]:
        """获取管理器状态"""
        with self._lock:
            return {
                "enabled": self.enabled,
                "service_connected": self.service_connected,
                "tracking_count": len(self.current_faces),
                "recognized_count": sum(1 for f in self.current_faces.values() if f.recognized),
                "primary_face_id": self.primary_face_id,
                "gaze_follow_enabled": self.gaze_config.get('enabled', True)
            }
    
    # ========== 人脸管理方法 (Face Management) ==========
    
    def set_confirmation_handler_tts(self, tts_client) -> None:
        """设置确认处理器的 TTS 客户端"""
        if self.confirmation_handler:
            self.confirmation_handler.tts_client = tts_client
            logger.debug("[FaceRecoManager] ConfirmationHandler TTS 已设置")
    
    # ========== 拍照/录像结果处理 ==========
    
    def _handle_capture_complete_event(self, event_data: Dict[str, Any]) -> bool:
        """
        处理拍照/录像完成事件
        
        Args:
            event_data: 事件数据
                {
                    'request_id': '...',
                    'type': 'photo' | 'video',
                    'success': True/False,
                    'file_path': '/path/to/file',
                    'file_size': 12345,
                    'resolution': {'width': 640, 'height': 480},
                    'faces_detected': 1,
                    'error': '...' (if failed)
                }
        
        Returns:
            是否处理成功
        """
        try:
            capture_type = event_data.get('type', 'unknown')
            success = event_data.get('success', False)
            request_id = event_data.get('request_id', '')
            
            logger.info(f"[FaceRecoManager] 📸 收到拍照/录像完成事件: type={capture_type}, success={success}")
            
            if not success:
                error = event_data.get('error', '未知错误')
                logger.error(f"[FaceRecoManager] ❌ 拍照/录像失败: {error}")
                if self.tts_client:
                    self.tts_client.speak(f"{'拍照' if capture_type == 'photo' else '录像'}失败")
                return False
            
            file_path = event_data.get('file_path', '')
            if not file_path:
                logger.error("[FaceRecoManager] ❌ 未返回文件路径")
                return False
            
            logger.info(f"[FaceRecoManager] ✅ 文件已保存: {file_path}")
            
            if capture_type == 'photo':
                return self._handle_photo_complete(event_data, file_path)
            elif capture_type == 'video':
                return self._handle_video_complete(event_data, file_path)
            else:
                logger.warning(f"[FaceRecoManager] ⚠️ 未知的捕获类型: {capture_type}")
                return False
                
        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 处理拍照/录像完成事件失败: {e}", exc_info=True)
            return False
    
    def _handle_photo_complete(self, event_data: Dict[str, Any], file_path: str) -> bool:
        """
        处理拍照完成
        
        Args:
            event_data: 事件数据
            file_path: 照片文件路径
        
        Returns:
            是否处理成功
        """
        try:
            logger.info(f"[FaceRecoManager] 📸 处理拍照完成: {file_path}")
            
            # 获取拍照配置
            photo_config = self.config.get('face_recognition', {}).get('photo', {})
            
            # 1. 显示照片到屏幕
            display_duration = photo_config.get('display_duration', 3.0)
            if display_duration > 0 and self.eye_client:
                display_scale = photo_config.get('display_scale', 0.3)
                display_side = photo_config.get('display_side', 'RIGHT')
                
                logger.info(f"[FaceRecoManager] 📺 显示照片: duration={display_duration}s, scale={display_scale}, side={display_side}")
                
                try:
                    # 使用同步方式发送叠加图片命令到 eyeEngine 并获取 overlay_id
                    response = self.eye_client._send_command(
                        "play_overlay_image_sync",
                        image=file_path,
                        delay_ms=0,
                        side=display_side,
                        scale=display_scale,
                        loop=False,
                        suspend_when_animating=True
                    )
                    
                    if response and response.get('success'):
                        overlay_id = response.get('overlay_id')
                        logger.info(f"[FaceRecoManager] ✅ 照片已发送到 eyeEngine 显示，overlay_id={overlay_id}")
                        
                        # ★★★ 设置定时器自动清除照片（使用线程池）★★★
                        if overlay_id:
                            def clear_photo_overlay():
                                try:
                                    # 延迟一段时间后清除
                                    time.sleep(display_duration)
                                    
                                    logger.info(f"[FaceRecoManager] ⏱️ 照片显示时间到，清除 overlay: {overlay_id}")
                                    clear_response = self.eye_client._send_command(
                                        "stop_overlay_sequence_sync",
                                        overlay_id=overlay_id
                                    )
                                    if clear_response and clear_response.get('success'):
                                        logger.info(f"[FaceRecoManager] ✅ 照片 overlay 已清除: {overlay_id}")
                                    else:
                                        logger.warning(f"[FaceRecoManager] ⚠️ 清除照片 overlay 失败: {overlay_id}")
                                except Exception as e:
                                    logger.error(f"[FaceRecoManager] ❌ 清除照片 overlay 异常: {e}", exc_info=True)
                            
                            # ★★★ 使用线程池而不是 Timer ★★★
                            import concurrent.futures
                            if not hasattr(self, '_executor'):
                                self._executor = concurrent.futures.ThreadPoolExecutor(max_workers=2, thread_name_prefix='PhotoTimer')
                            
                            self._executor.submit(clear_photo_overlay)
                            logger.info(f"[FaceRecoManager] ⏱️ 已在后台线程设置 {display_duration} 秒后自动清除照片")
                    else:
                        logger.error(f"[FaceRecoManager] ❌ 照片显示失败: {response}")
                        
                except Exception as e:
                    logger.error(f"[FaceRecoManager] ❌ 显示照片失败: {e}", exc_info=True)
            
            # 2. 发送邮件（如果配置了）
            auto_send_email = photo_config.get('auto_send_email', False)
            email_recipients = photo_config.get('email_recipients', [])
            
            if auto_send_email and email_recipients:
                logger.info(f"[FaceRecoManager] 📧 准备发送照片到邮箱: {email_recipients}")
                self._send_photo_email(file_path, email_recipients, event_data)
            
            # 3. TTS 反馈
            if self.tts_client:
                faces_count = event_data.get('faces_detected', 0)
                if faces_count > 0:
                    self.tts_client.speak(f"拍照完成，检测到{faces_count}张人脸")
                else:
                    self.tts_client.speak("拍照完成")
            
            return True
            
        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 处理拍照完成失败: {e}", exc_info=True)
            return False
    
    def _handle_video_complete(self, event_data: Dict[str, Any], file_path: str) -> bool:
        """
        处理录像完成
        
        Args:
            event_data: 事件数据
            file_path: 视频文件路径
        
        Returns:
            是否处理成功
        """
        try:
            logger.info(f"[FaceRecoManager] 🎥 处理录像完成: {file_path}")
            
            # ★★★ 检查视频文件大小，诊断 FaceReco 的 video_record 问题 ★★★
            import os
            file_size = 0
            if os.path.exists(file_path):
                file_size = os.path.getsize(file_path)
                logger.info(f"[FaceRecoManager] 📊 视频文件大小: {file_size} 字节")
                
                # 检测空文件或过小文件
                if file_size < 1024:  # 小于 1KB
                    logger.error(f"[FaceRecoManager] ❌ 视频文件为空或过小 ({file_size} 字节)")
                    logger.error("[FaceRecoManager] ⚠️ 诊断: FaceReco 的 video_record 模块可能未实现")
                    
                    if self.tts_client:
                        self.tts_client.speak("录像失败：视频保存出错，功能暂不可用")
                    
                    return False
            else:
                logger.error(f"[FaceRecoManager] ❌ 视频文件不存在: {file_path}")
                if self.tts_client:
                    self.tts_client.speak("录像失败：文件未生成")
                return False
            
            # 获取录像配置
            video_config = self.config.get('face_recognition', {}).get('video', {})
            
            # 发送邮件（如果配置了）
            auto_send_email = video_config.get('auto_send_email', False)
            email_recipients = video_config.get('email_recipients', [])
            
            if auto_send_email and email_recipients:
                logger.info(f"[FaceRecoManager] 📧 准备发送视频到邮箱: {email_recipients}")
                self._send_video_email(file_path, email_recipients, event_data)
            
            # TTS 反馈
            if self.tts_client:
                self.tts_client.speak("录像完成")
            
            return True
            
        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 处理录像完成失败: {e}", exc_info=True)
            return False
    
    def _handle_capture_started_event(self, event_data: Dict[str, Any]) -> bool:
        """
        处理拍照/录像开始事件
        
        Args:
            event_data: 事件数据
        
        Returns:
            是否处理成功
        """
        try:
            capture_type = event_data.get('type', 'unknown')
            logger.info(f"[FaceRecoManager] 📹 拍照/录像已开始: type={capture_type}")
            return True
        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 处理拍照/录像开始事件失败: {e}", exc_info=True)
            return False
    
    def _send_photo_email(self, photo_path: str, recipients: List[str], event_data: Dict[str, Any]) -> bool:
        """
        发送照片到邮箱
        
        Args:
            photo_path: 照片路径
            recipients: 收件人列表
            event_data: 事件数据
        
        Returns:
            是否发送成功
        """
        try:
            # 延迟导入邮件配置和发送器
            email_config_path = Path("/home/pi/dolydev/config/email_settings.yaml")
            if not email_config_path.exists():
                logger.error("[FaceRecoManager] ❌ 邮件配置文件不存在")
                return False
            
            with open(email_config_path, 'r', encoding='utf-8') as f:
                email_config = yaml.safe_load(f) or {}
            
            smtp_config = email_config.get('email', {}).get('smtp', {})
            sender_config = email_config.get('email', {}).get('sender', {})
            
            if not smtp_config.get('enabled', False):
                logger.warning("[FaceRecoManager] ⚠️ 邮件功能未启用")
                return False
            
            # 创建邮件发送器
            from modules.doly.utils.email_sender import EmailSender
            email_sender = EmailSender({
                'smtp_server': smtp_config.get('server', ''),
                'smtp_port': smtp_config.get('port', 587),
                'smtp_username': smtp_config.get('username', ''),
                'smtp_password': smtp_config.get('password', ''),
                'from_email': sender_config.get('email', smtp_config.get('username', '')),
                'from_name': sender_config.get('name', 'Doly Robot')
            })
            
            # 发送照片
            import datetime
            timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            success = email_sender.send_photo(
                to_addrs=recipients,
                photo_path=photo_path,
                subject=f"📸 来自 Doly 的照片 - {timestamp}",
                message=f"这是 Doly 机器人在 {timestamp} 拍摄的照片。\n\n文件名: {Path(photo_path).name}"
            )
            
            if success:
                logger.info(f"[FaceRecoManager] ✅ 照片已发送到邮箱: {recipients}")
                if self.tts_client:
                    self.tts_client.speak("照片已发送到您的邮箱")
            else:
                logger.error("[FaceRecoManager] ❌ 照片发送失败")
                if self.tts_client:
                    self.tts_client.speak("照片发送失败")
            
            return success
            
        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 发送照片邮件失败: {e}", exc_info=True)
            return False
    
    def _send_video_email(self, video_path: str, recipients: List[str], event_data: Dict[str, Any]) -> bool:
        """
        发送视频到邮箱
        
        Args:
            video_path: 视频路径
            recipients: 收件人列表
            event_data: 事件数据
        
        Returns:
            是否发送成功
        """
        try:
            # 延迟导入邮件配置和发送器
            email_config_path = Path("/home/pi/dolydev/config/email_settings.yaml")
            if not email_config_path.exists():
                logger.error("[FaceRecoManager] ❌ 邮件配置文件不存在")
                return False
            
            with open(email_config_path, 'r', encoding='utf-8') as f:
                email_config = yaml.safe_load(f) or {}
            
            smtp_config = email_config.get('email', {}).get('smtp', {})
            sender_config = email_config.get('email', {}).get('sender', {})
            
            if not smtp_config.get('enabled', False):
                logger.warning("[FaceRecoManager] ⚠️ 邮件功能未启用")
                return False
            
            # 创建邮件发送器
            from modules.doly.utils.email_sender import EmailSender
            email_sender = EmailSender({
                'smtp_server': smtp_config.get('server', ''),
                'smtp_port': smtp_config.get('port', 587),
                'smtp_username': smtp_config.get('username', ''),
                'smtp_password': smtp_config.get('password', ''),
                'from_email': sender_config.get('email', smtp_config.get('username', '')),
                'from_name': sender_config.get('name', 'Doly Robot')
            })
            
            # 发送视频
            import datetime
            timestamp = datetime.datetime.now().strftime("%Y-%m-%d %H:%M:%S")
            success = email_sender.send_video(
                to_addrs=recipients,
                video_path=video_path,
                subject=f"🎥 来自 Doly 的视频 - {timestamp}",
                message=f"这是 Doly 机器人在 {timestamp} 录制的视频。\n\n文件名: {Path(video_path).name}"
            )
            
            if success:
                logger.info(f"[FaceRecoManager] ✅ 视频已发送到邮箱: {recipients}")
                if self.tts_client:
                    self.tts_client.speak("视频已发送到您的邮箱")
            else:
                logger.error("[FaceRecoManager] ❌ 视频发送失败")
                if self.tts_client:
                    self.tts_client.speak("视频发送失败")
            
            return success
            
        except Exception as e:
            logger.error(f"[FaceRecoManager] ❌ 发送视频邮件失败: {e}", exc_info=True)
            return False


# ============================================================================
# FaceDBClient 类已删除（架构重构）
# ============================================================================
# 
# 删除原因：
#   - 职责划分错误：Daemon 不应维护独立的人脸数据库
#   - 数据持久化缺失：原实现仅使用内存存储，重启后丢失
#   - 架构混乱：Vision Service 已有 FaceDatabase，不应重复实现
# 
# 新架构：
#   Daemon (交互层)
#     ↓ 发送 ZMQ 命令
#   Vision Service (数据层)
#     ↓ FaceDatabase (C++ 持久化)
#   JSON 文件存储
# 
# 支持的 ZMQ 命令：
#   - cmd.vision.face.register → event.vision.face.registered
#   - cmd.vision.face.update   → event.vision.face.updated
#   - cmd.vision.face.delete   → event.vision.face.deleted
#   - cmd.vision.face.query    → 返回查询结果
# 
# Vision Service 实现位置：
#   - libs/FaceReco/include/doly/vision/face_database.hpp
#   - libs/FaceReco/src/face_database.cpp
#   - libs/FaceReco/src/vision_service.cpp (handleFaceCommand)
# 
# 重构日期：2026-02-10
# ============================================================================

