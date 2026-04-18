"""
语音指令管理器

处理语音指令到动作的映射和执行

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import yaml
import logging
from typing import Optional, Dict, Any, Callable
from pathlib import Path
import zmq

logger = logging.getLogger(__name__)


class VoiceCommandManager:
    """语音指令管理器"""
    
    def __init__(self, config_path: str = "/home/pi/dolydev/config/voice_command_mapping.yaml"):
        """
        初始化语音指令管理器
        
        Args:
            config_path: 配置文件路径
        """
        self.config_path = Path(config_path)
        self.mappings: Dict[str, Dict[str, Any]] = {}
        self.handlers: Dict[str, Callable] = {}
        
        # ★ 新增：防抖/冷却机制 ★
        self._last_command_id: Optional[str] = None
        self._last_command_time: float = 0
        self._cooldown_seconds: float = 3.0  # 相同命令 3 秒冷却
        
        self._load_config()
        logger.info(f"VoiceCommandManager 初始化完成, 加载了 {len(self.mappings)} 条指令映射")
    
    def _load_config(self):
        """加载配置文件"""
        try:
            with open(self.config_path, 'r', encoding='utf-8') as f:
                data = yaml.safe_load(f)
            
            # 提取 mappings 或 commands 部分
            if 'mappings' in data:
                self.mappings = data['mappings']
            elif 'commands' in data:
                self.mappings = data['commands']
            else:
                self.mappings = data
            
            logger.info(f"加载语音指令配置成功: {self.config_path}")
        except Exception as e:
            logger.error(f"加载语音指令配置失败: {e}")
            self.mappings = {}
    
    def register_handler(self, action_type: str, handler: Callable):
        """
        注册动作处理器
        
        Args:
            action_type: 动作类型(widget_control, timer_control, etc.)
            handler: 处理函数
        """
        self.handlers[action_type] = handler
        logger.info(f"注册动作处理器: {action_type}")
    
    def handle_command(self, command_id: str, **context) -> bool:
        """
        处理语音指令
        
        Args:
            command_id: 指令ID(如 cmd_ActTime)
            **context: 上下文参数
            
        Returns:
            是否成功处理
        """
        import time
        now = time.time()
        
        # ★ 防抖检查 ★
        if command_id == self._last_command_id:
            elapsed = now - self._last_command_time
            if elapsed < self._cooldown_seconds:
                logger.warning(f"⏩ [Voice] 忽略重复的语音指令 (冷却中): {command_id}, 剩余 {self._cooldown_seconds - elapsed:.1f}s")
                return True # 返回 True 表示已经"处理"了（忽略也是处理）
        
        # 查找指令映射
        mapping = self.mappings.get(command_id)
        if not mapping:
            logger.warning(f"未找到指令映射: {command_id}")
            return False
        
        action_type = mapping.get('action')
        if not action_type:
            logger.warning(f"指令缺少 action 字段: {command_id}")
            return False
        
        # 查找对应的处理器
        handler = self.handlers.get(action_type)
        if not handler:
            logger.warning(f"未注册处理器: {action_type}, command={command_id}")
            return False
        
        # 调用处理器
        try:
            logger.info(f"处理语音指令: {command_id}, action={action_type}")
            
            # 更新最后一次指令信息
            self._last_command_id = command_id
            self._last_command_time = now
            
            result = handler(command_id=command_id, mapping=mapping, **context)
            return result
        except Exception as e:
            logger.error(f"处理语音指令异常: {command_id}, error={e}", exc_info=True)
            return False
    
    def get_command_info(self, command_id: str) -> Optional[Dict[str, Any]]:
        """
        获取指令信息
        
        Args:
            command_id: 指令ID
            
        Returns:
            指令配置字典
        """
        return self.mappings.get(command_id)
    
    def get_commands_by_category(self, category: str) -> Dict[str, Dict[str, Any]]:
        """
        按分类获取指令
        
        Args:
            category: 分类(widget, timer_control, etc.)
            
        Returns:
            指令字典
        """
        return {
            cmd_id: mapping
            for cmd_id, mapping in self.mappings.items()
            if mapping.get('category') == category
        }
    
    def reload_config(self):
        """重新加载配置"""
        self._load_config()
        logger.info("语音指令配置已重新加载")


class VoiceCommandHandlers:
    """语音指令处理器集合"""
    
    def __init__(self, widget_manager, animation_manager=None, daemon=None):
        """
        初始化处理器集合
        
        Args:
            widget_manager: WidgetManager 实例
            animation_manager: AnimationManager 实例(可选)
            daemon: DolyDaemon 实例(可选，用于 direct_command 和 skill)
        """
        self.widget_manager = widget_manager
        self.animation_manager = animation_manager
        self.daemon = daemon
        
        # ========== Eye 样式循环状态管理 ==========
        self._current_lid_style_index = 0
        self._current_iris_theme_index = 0
        self._current_iris_style_index = 0
        self._lid_styles = []  # 可用眼睑样式列表
        self._iris_themes = {}  # 可用虹膜主题和样式 {theme: [styles]}
        self._iris_initialized = False
        
        logger.info("VoiceCommandHandlers 初始化完成")
    
    def handle_widget_control(self, command_id: str, mapping: Dict[str, Any], **context) -> bool:
        """
        处理 widget_control 动作
        
        Args:
            command_id: 指令ID
            mapping: 指令配置
            **context: 上下文
            
        Returns:
            是否成功
        """
        widget_type = mapping.get('widget')
        timeout = mapping.get('timeout')
        tts = mapping.get('tts')
        
        # 生成 TTS 文本
        tts_text = None
        if tts:
            if isinstance(tts, str):
                tts_text = tts
            elif tts is True:
                # 如果是布尔值 True，直接透传，以便触发 WidgetManager 里的 announce_time 逻辑
                tts_text = True
        
        # 根据 widget 类型调用相应方法
        if widget_type == "clock":
            return self.widget_manager.show_clock(timeout=timeout, tts=tts_text)
        
        elif widget_type == "date":
            return self.widget_manager.show_date(timeout=timeout, tts=tts_text)
        
        elif widget_type == "countdown":
            duration = mapping.get('duration', 30)
            auto_start = mapping.get('auto_start', False)
            return self.widget_manager.start_countdown(
                duration=duration, auto_start=auto_start, timeout=timeout, tts=tts_text
            )
        
        elif widget_type == "timer":
            auto_start = mapping.get('auto_start', True)
            return self.widget_manager.start_timer(auto_start=auto_start, timeout=timeout, tts=tts_text)
        
        elif widget_type == "alarm":
            # TODO: 从 context 或 mapping 中获取时间
            hour = mapping.get('hour', 8)
            minute = mapping.get('minute', 0)
            return self.widget_manager.set_alarm(hour=hour, minute=minute, timeout=timeout, tts=tts_text)
        
        elif widget_type == "weather":
            location = mapping.get('location')
            return self.widget_manager.show_weather(location=location, timeout=timeout, tts=tts_text)
        
        else:
            logger.warning(f"未知的 widget 类型: {widget_type}, command={command_id}")
            return False
    
    def handle_timer_control(self, command_id: str, mapping: Dict[str, Any], **context) -> bool:
        """
        处理 timer_control 动作
        
        Args:
            command_id: 指令ID
            mapping: 指令配置
            **context: 上下文
            
        Returns:
            是否成功
        """
        command = mapping.get('command')
        tts = mapping.get('tts')
        
        # 生成 TTS 文本
        tts_text = None
        if tts:
            if isinstance(tts, str):
                tts_text = tts
            elif tts is True:
                tts_text = self._generate_control_tts(command)
        
        return self.widget_manager.timer_control(action=command, tts=tts_text)
    
    def _generate_default_tts(self, widget_type: str) -> Optional[str]:
        """生成默认 TTS 文本"""
        tts_map = {
            "clock": "现在是几点了",
            "date": "今天的日期",
            "countdown": "倒计时准备好了",
            "timer": "计时器已启动",
            "alarm": "闹钟已设置",
            "weather": "天气信息"
        }
        return tts_map.get(widget_type)
    
    def _generate_control_tts(self, command: str) -> Optional[str]:
        """生成控制指令 TTS 文本"""
        tts_map = {
            "start": "开始",
            "cancel": "已取消",
            "pause": "已暂停",
            "resume": "继续"
        }
        return tts_map.get(command)
    
    def handle_play_animation(self, command_id: str, mapping: Dict[str, Any], **context) -> bool:
        """
        处理 play_animation 动作
        
        Args:
            command_id: 指令ID
            mapping: 指令配置
            **context: 上下文
            
        Returns:
            是否成功
        """
        if not self.animation_manager:
            logger.warning(f"[PlayAnimation] AnimationManager 未设置，无法播放: {command_id}")
            return False
        
        animation = mapping.get('animation')
        priority_str = mapping.get('priority', 'normal')
        
        # 转换优先级字符串到数字
        priority_map = {'low': 3, 'normal': 5, 'high': 8}
        priority = priority_map.get(priority_str, 5)
        
        if not animation:
            logger.warning(f"[PlayAnimation] 缺少 animation 字段: {command_id}")
            return False
        
        # ★★★ 添加详细日志 ★★★
        logger.info(f"🎬 [PlayAnimation] 准备播放: command={command_id}, animation={animation}, priority={priority}")
        
        # 调用 AnimationManager 播放动画
        result = self.animation_manager.play_animation(animation, priority=priority)
        
        if result:
            logger.info(f"✅ [PlayAnimation] 播放成功: {command_id}")
        else:
            logger.error(f"❌ [PlayAnimation] 播放失败: {command_id}")
        
        return result
    
    def handle_direct_command(self, command_id: str, mapping: Dict[str, Any], **context) -> bool:
        """
        处理 direct_command 动作
        
        Args:
            command_id: 指令ID
            mapping: 指令配置
            **context: 上下文
            
        Returns:
            是否成功
        """
        if not self.daemon:
            logger.warning(f"[DirectCommand] Daemon 未设置，无法执行: {command_id}")
            return False
        
        target = mapping.get('target')
        command = mapping.get('command')
        params = mapping.get('params', {})
        
        if not target or not command:
            logger.warning(f"[DirectCommand] 缺少 target 或 command: {command_id}")
            return False
        
        logger.info(f"[DirectCommand] 执行: target={target}, command={command}, params={params}")
        
        # 路由到 Eye 命令处理器
        if target == 'eye':
            return self._handle_eye_command(command, params)
        elif target == 'music':
            return self._handle_music_command(command, params)
        elif target == 'audio_system':
            return self._handle_audio_system_command(command, params)
        
        # TODO: 根据 target 路由到不同的模块
        # 目前作为占位实现
        elif target == 'drive' and hasattr(self.daemon, 'drive_client'):
            # 示例：移动控制
            logger.info(f"[DirectCommand] 驱动命令: {command}")
            return True
        
        elif target == 'system':
            if command == 'interrupt':
                logger.info("[DirectCommand] 中断当前动作")
                # 中断动画
                if self.animation_manager:
                    # TODO: 实现 interrupt 方法
                    pass
                return True
        
        logger.warning(f"[DirectCommand] 未实现的命令: target={target}, command={command}")
        return False


    def _handle_music_command(self, command: str, params: Dict[str, Any]) -> bool:
        """通过 ZMQ 调用 liteplayer 音乐服务。"""
        # command 映射
        cmd_map = {
            'play': 'play',
            'pause': 'pause',
            'stop': 'stop',
            'resume': 'play',   # liteplayer 当前无独立 resume，play 在 Paused 状态可恢复
            'next': 'next',
            'previous': 'previous',
            'set_mode': 'set_play_mode',
        }
        cmd = cmd_map.get(command)
        if not cmd:
            logger.warning(f"[MusicCommand] 未知命令: {command}")
            return False

        endpoint = 'ipc:///tmp/music_player_cmd.sock'
        req = {
            'command': cmd,
            'params': params or {}
        }

        ctx = zmq.Context.instance()
        sock = ctx.socket(zmq.REQ)
        sock.setsockopt(zmq.RCVTIMEO, 2000)
        sock.setsockopt(zmq.SNDTIMEO, 2000)
        try:
            sock.connect(endpoint)
            logger.info(f"[MusicCommand] => {req}")
            sock.send_json(req)
            rsp = sock.recv_json()
            ok = rsp.get('status') == 'success'
            if ok:
                logger.info(f"[MusicCommand] ✅ success: {command}")
            else:
                logger.warning(f"[MusicCommand] ❌ failed: {rsp}")
            return ok
        except Exception as e:
            logger.error(f"[MusicCommand] ZMQ error: {e}")
            return False
        finally:
            sock.close(0)
    
    def _handle_audio_system_command(self, command: str, params: Dict[str, Any]) -> bool:
        """
        处理音量控制命令
        
        Args:
            command: 命令名称 (volume_up, volume_down, mute, unmute)
            params: 参数字典 (可能包含 step 参数)
            
        Returns:
            是否成功
        """
        if not hasattr(self.daemon, 'audio_volume_manager'):
            logger.warning("[AudioSystem] AudioVolumeManager 未初始化")
            return False
        
        manager = self.daemon.audio_volume_manager
        
        try:
            if command == 'volume_up':
                step = params.get('step') if params else None
                result = manager.volume_up(step=step)
                logger.info(f"🔊 [AudioSystem] 音量增加: {result}")
                return True
            
            elif command == 'volume_down':
                step = params.get('step') if params else None
                result = manager.volume_down(step=step)
                logger.info(f"🔉 [AudioSystem] 音量减少: {result}")
                return True
            
            elif command == 'mute':
                result = manager.mute()
                logger.info(f"🔇 [AudioSystem] 静音: {result}")
                return True
            
            elif command == 'unmute':
                result = manager.unmute()
                logger.info(f"🔊 [AudioSystem] 取消静音: {result}")
                return True
            
            elif command == 'set_volume':
                volume = params.get('volume') if params else None
                if volume is None:
                    logger.warning("[AudioSystem] set_volume 命令缺少 volume 参数")
                    return False
                result = manager.set_volume(volume)
                logger.info(f"🔊 [AudioSystem] 设置音量: {result}")
                return True
            
            else:
                logger.warning(f"[AudioSystem] 未知命令: {command}")
                return False
        
        except Exception as e:
            logger.error(f"[AudioSystem] 处理命令失败: {command}, error={e}", exc_info=True)
            return False
    
    def handle_skill(self, command_id: str, mapping: Dict[str, Any], **context) -> bool:
        """
        处理 skill 动作（技能调用）
        
        支持的技能：
        - face_recognition: 人脸识别
        - photo_capture: 拍照
        - video_capture: 录像
        
        Args:
            command_id: 指令ID
            mapping: 指令配置
            **context: 上下文
            
        Returns:
            是否成功
        """
        target = mapping.get('target')
        command = mapping.get('command')
        params = mapping.get('params', {})
        
        logger.info(f"[Skill] 调用技能: target={target}, command={command}, params={params}")
        
        if not self.daemon:
            logger.warning(f"[Skill] Daemon 未设置，无法执行: {command_id}")
            return False
        
        # 人脸识别相关技能
        if target == 'face_recognition':
            return self._handle_face_recognition_skill(command, params, mapping)
        
        # 其它技能...
        logger.warning(f"[Skill] 未知的技能目标: {target}")
        return False
    
    def _handle_face_recognition_skill(self, command: str, params: Dict, mapping: Dict) -> bool:
        """
        处理人脸识别相关技能
        
        Args:
            command: 命令（recognize, capture_photo, capture_video）
            params: 参数
            mapping: 完整映射配置
            
        Returns:
            是否成功
        """
        # 获取 FaceRecoManager
        face_manager = getattr(self.daemon, 'face_reco_manager', None)
        
        if command == 'recognize':
            # cmd_ActWhoami: 触发人脸识别（切换 Vision Service 到 FULL 模式）
            logger.info("[FaceRecognition] 🔍 触发人脸识别功能 (cmd_ActWhoami)")
            
            # 播放提示动画（如果配置）
            animation = mapping.get('animation')
            if animation and self.animation_manager:
                self.animation_manager.play_animation(animation, priority=7)
            
            # ★★★ 调用 FaceRecoManager 的 handle_whoami_command 方法 ★★★
            if face_manager and hasattr(face_manager, 'handle_whoami_command'):
                result = face_manager.handle_whoami_command()
                logger.info(f"[FaceRecognition] {'✅ 成功' if result else '❌ 失败'} 切换到 FULL 模式")
                return result
            else:
                logger.warning("[FaceRecognition] ⚠️ FaceRecoManager 未设置或缺少 handle_whoami_command 方法")
                return False
        
        elif command == 'capture_photo':
            # cmd_ActTakePhoto: 拍照
            logger.info("[FaceRecognition] 📸 触发拍照功能 (cmd_ActTakePhoto)")
            
            # 播放拍照动画
            animation = mapping.get('animation')
            if animation and self.animation_manager:
                self.animation_manager.play_animation(animation, priority=7)
            
            # ★★★ 调用 FaceRecoManager 的 handle_take_photo_command 方法 ★★★
            if face_manager and hasattr(face_manager, 'handle_take_photo_command'):
                result = face_manager.handle_take_photo_command(**params)
                logger.info(f"[FaceRecognition] {'✅ 拍照成功' if result else '❌ 拍照失败'}")
                return result
            else:
                logger.warning("[FaceRecognition] ⚠️ FaceRecoManager 未设置或缺少 handle_take_photo_command 方法")
                return False
        
        elif command == 'capture_video':
            # cmd_ActTakeVideo: 录像
            duration = params.get('duration', 10)
            logger.info(f"[FaceRecognition] 🎥 触发录像功能 (cmd_ActTakeVideo, {duration}秒)")
            
            # 播放动画
            animation = mapping.get('animation')
            if animation and self.animation_manager:
                self.animation_manager.play_animation(animation, priority=7)
            
            # ★★★ 调用 FaceRecoManager 的 handle_take_video_command 方法 ★★★
            if face_manager and hasattr(face_manager, 'handle_take_video_command'):
                # 移除 params 中的 duration 避免重复
                filtered_params = {k: v for k, v in params.items() if k != 'duration'}
                result = face_manager.handle_take_video_command(duration=duration, **filtered_params)
                logger.info(f"[FaceRecognition] {'✅ 录像成功' if result else '❌ 录像失败'}")
                return result
            else:
                logger.warning("[FaceRecognition] ⚠️ FaceRecoManager 未设置或缺少 handle_take_video_command 方法")
                return False
        
        elif command == 'register':
            # cmd_RegisterFace: 注册人脸
            logger.info("[FaceRecognition] 📝 触发注册人脸功能 (cmd_RegisterFace)")
            
            if face_manager and hasattr(face_manager, 'handle_register_face_command'):
                # 直接调用同步方法
                result = face_manager.handle_register_face_command(params)
                logger.info(f"[FaceRecognition] {'✅ 注册流程启动' if result else '❌ 注册失败'}")
                return result
            else:
                logger.warning("[FaceRecognition] ⚠️ FaceRecoManager 未设置或缺少 handle_register_face_command 方法")
                return False
        
        elif command == 'update':
            # cmd_UpdateFace: 更新人脸信息
            logger.info("[FaceRecognition] ✏️ 触发更新人脸功能 (cmd_UpdateFace)")
            
            if face_manager and hasattr(face_manager, 'handle_update_face_command'):
                # 直接调用同步方法
                result = face_manager.handle_update_face_command(params)
                logger.info(f"[FaceRecognition] {'✅ 更新流程启动' if result else '❌ 更新失败'}")
                return result
            else:
                logger.warning("[FaceRecognition] ⚠️ FaceRecoManager 未设置或缺少 handle_update_face_command 方法")
                return False
        
        elif command == 'delete':
            # cmd_DeleteFace: 删除人脸
            logger.info("[FaceRecognition] 🗑️ 触发删除人脸功能 (cmd_DeleteFace)")
            
            if face_manager and hasattr(face_manager, 'handle_delete_face_command'):
                # 直接调用同步方法
                result = face_manager.handle_delete_face_command(params)
                logger.info(f"[FaceRecognition] {'✅ 删除流程启动' if result else '❌ 删除失败'}")
                return result
            else:
                logger.warning("[FaceRecognition] ⚠️ FaceRecoManager 未设置或缺少 handle_delete_face_command 方法")
                return False
        
        elif command == 'query':
            # cmd_QueryFace: 查询人脸列表
            logger.info("[FaceRecognition] 🔍 触发查询人脸功能 (cmd_QueryFace)")
            
            if face_manager and hasattr(face_manager, 'handle_query_face_command'):
                result = face_manager.handle_query_face_command(params)
                logger.info(f"[FaceRecognition] {'✅ 查询成功' if result else '❌ 查询失败'}")
                return result
            else:
                logger.warning("[FaceRecognition] ⚠️ FaceRecoManager 未设置或缺少 handle_query_face_command 方法")
                return False
        
        else:
            logger.warning(f"[FaceRecognition] ❌ 未知的人脸识别命令: {command}")
            return False
    
    def handle_confirmation(self, command_id: str, mapping: Dict[str, Any], **context) -> bool:
        """
        处理确认/取消命令（cmd_Confirm / cmd_Cancel）
        
        Args:
            command_id: 指令ID（cmd_Confirm 或 cmd_Cancel）
            mapping: 指令配置
            **context: 上下文
            
        Returns:
            是否成功
        """
        confirmed = mapping.get('value', False)
        logger.info(f"[Confirmation] {'✅ 确认' if confirmed else '❌ 取消'} 命令: {command_id}")
        
        # 获取 FaceRecoManager
        face_manager = getattr(self.daemon, 'face_reco_manager', None)
        
        if face_manager and hasattr(face_manager, 'confirmation_handler'):
            confirmation_handler = face_manager.confirmation_handler
            if confirmation_handler:
                result = confirmation_handler.handle_response(confirmed)
                logger.info(f"[Confirmation] {'✅ 处理成功' if result else '⚠️ 无待确认操作'}")
                return result
            else:
                logger.warning("[Confirmation] ⚠️ ConfirmationHandler 未初始化")
                return False
        else:
            logger.warning("[Confirmation] ⚠️ FaceRecoManager 或 ConfirmationHandler 未设置")
            return False
    
    def handle_face_management(self, command_id: str, mapping: Dict[str, Any], **context) -> bool:
        """
        处理人脸管理命令（register, update, delete, query）
        
        Args:
            command_id: 指令ID（如 cmd_RegisterFace）
            mapping: 指令配置
            **context: 上下文
            
        Returns:
            是否成功
        """
        operation = mapping.get('operation')  # register / update / delete / query
        params = context
        
        logger.info(f"[FaceManagement] 处理人脸管理命令: {command_id}, operation={operation}")
        
        # 获取 FaceRecoManager
        face_manager = getattr(self.daemon, 'face_reco_manager', None)
        
        if not face_manager:
            logger.warning("[FaceManagement] ⚠️ FaceRecoManager 未设置")
            return False
        
        # 路由到相应的处理方法
        if operation == 'register':
            return self._handle_face_recognition_skill('register', params, mapping)
        elif operation == 'update':
            return self._handle_face_recognition_skill('update', params, mapping)
        elif operation == 'delete':
            return self._handle_face_recognition_skill('delete', params, mapping)
        elif operation == 'query':
            return self._handle_face_recognition_skill('query', params, mapping)
        else:
            logger.warning(f"[FaceManagement] ❌ 未知的人脸管理操作: {operation}")
            return False
    
    def handle_photo(self, command_id: str, mapping: Dict[str, Any], **context) -> bool:
        """
        处理 photo 动作
        
        Args:
            command_id: 指令ID
            mapping: 指令配置
            **context: 上下文
            
        Returns:
            是否成功
        """
        logger.info(f"[Photo] 拍照指令: {command_id}")
        
        # 如果配置了动画，播放动画
        animation = mapping.get('animation')
        if animation and self.animation_manager:
            print(f"------> [Photo] 播放拍照动画: {animation}")
            self.animation_manager.play_animation(animation, priority=7)
        
        # TODO: 调用相机模块
        logger.info("[Photo] TODO: 调用相机模块")
        return True
    
    # ========== Eye 控制命令处理器 ==========
    
    async def _init_eye_styles(self):
        """
        初始化眼睛样式列表（从 eyeEngine 查询）
        """
        if self._iris_initialized:
            return
        
        try:
            # 获取 eye_interface
            eye_interface = None
            if self.animation_manager and hasattr(self.animation_manager, 'eye_interface'):
                eye_interface = self.animation_manager.eye_interface
            
            if not eye_interface:
                logger.warning("[EyeStyles] 无法获取 eye_interface，使用默认配置")
                # 使用默认配置
                self._iris_themes = {
                    "默认主题": ["normal", "cat", "mech"],
                    "彩色主题": ["blue", "green", "red"]
                }
                self._lid_styles = ["default", "cat", "mech"]
                self._iris_initialized = True
                return
            
            # 查询虹膜样式
            self._iris_themes = await eye_interface.list_iris()
            logger.info(f"[EyeStyles] 已加载 {len(self._iris_themes)} 个虹膜主题")
            
            # TODO: 查询眼睑样式（如果有相应 API）
            # self._lid_styles = await eye_interface.list_lids()
            # 暂时使用默认列表
            self._lid_styles = ["default", "cat", "mech", "narrow", "wide"]
            
            self._iris_initialized = True
            logger.info("[EyeStyles] 样式列表初始化完成")
            
        except Exception as e:
            logger.error(f"[EyeStyles] 初始化失败: {e}")
            # 使用默认配置
            self._iris_themes = {"默认主题": ["normal"]}
            self._lid_styles = ["default"]
            self._iris_initialized = True
    
    def _handle_eye_command(self, command: str, params: Dict[str, Any]) -> bool:
        """
        处理眼睛控制命令（同步包装器）
        
        支持的命令:
        - set_lid: 设置眼睑
        - set_iris: 设置虹膜
        - set_brightness: 设置亮度
        - blink: 眨眼
        - squint: 眯眼
        - gaze: 凝视
        - circle: 转眼珠
        - cross_eyed: 斗鸡眼
        - cycle_lid_style: 循环眼睑样式
        - cycle_iris_style: 循环虹膜样式
        
        Args:
            command: 命令名称
            params: 命令参数
            
        Returns:
            是否成功
        """
        # 检查是否有 animation_manager
        if not self.animation_manager or not hasattr(self.animation_manager, 'eye_interface'):
            logger.warning(f"[EyeCommand] AnimationManager 或 eye_interface 未设置")
            return False
        
        eye_interface = self.animation_manager.eye_interface
        
        try:
            import asyncio
            
            # 使用 asyncio.run 执行异步命令
            if command == 'set_brightness':
                asyncio.run(eye_interface.set_brightness(
                    level=params.get('level', 128),
                    side=params.get('side', 'BOTH')
                ))
                logger.info(f"[EyeCommand] ✅ set_brightness 执行成功: {params}")
                
            elif command == 'blink':
                asyncio.run(eye_interface.blink(
                    count=params.get('count', 1),
                    side=params.get('side', 'BOTH'),
                    duration=params.get('duration', 200)
                ))
                logger.info(f"[EyeCommand] ✅ blink 执行成功: {params}")
                
            elif command == 'squint':
                # 眯眼 - 通过播放动画实现
                amount = params.get('amount', 0.5)
                eye = params.get('eye', 'both')
                logger.info(f"[EyeCommand] squint (通过动画): amount={amount}, eye={eye}")
                # 可以播放眯眼动画
                if self.animation_manager:
                    animation_name = "squint_1.xml"  # 需要配置对应的动画
                    self.animation_manager.play_animation(animation_name, priority=6)
                return True
                
            elif command == 'gaze':
                # 凝视 - 通过播放动画实现
                x = params.get('x', 0)
                y = params.get('y', 0)
                logger.info(f"[EyeCommand] gaze (通过动画): x={x}, y={y}")
                # 根据 x, y 选择对应的眼睛动画
                if y < -0.3:
                    animation = "look_up.xml"
                elif y > 0.3:
                    animation = "look_down.xml"
                elif x < -0.3:
                    animation = "look_left.xml"
                elif x > 0.3:
                    animation = "look_right.xml"
                else:
                    animation = "look_center.xml"
                
                if self.animation_manager:
                    self.animation_manager.play_animation(animation, priority=6)
                return True
                
            elif command == 'circle':
                # 转眼珠 - 播放动画
                direction = params.get('direction', 'cw')
                cycles = params.get('cycles', 1)
                logger.info(f"[EyeCommand] circle: direction={direction}, cycles={cycles}")
                if self.animation_manager:
                    animation = "eye_circle_cw.xml" if direction == 'cw' else "eye_circle_ccw.xml"
                    self.animation_manager.play_animation(animation, priority=6)
                return True
                
            elif command == 'cross_eyed':
                # 斗鸡眼 - 播放动画
                duration = params.get('duration', 2000)
                logger.info(f"[EyeCommand] cross_eyed: duration={duration}ms")
                if self.animation_manager:
                    self.animation_manager.play_animation("cross_eyed.xml", priority=6)
                return True
                
            elif command == 'cycle_lid_style':
                # 循环眼睑样式
                asyncio.run(self._handle_cycle_lid_style(params))
                
            elif command == 'cycle_iris_style':
                # 循环虹膜样式
                asyncio.run(self._handle_cycle_iris_style(params))
                
            else:
                logger.warning(f"[EyeCommand] 未知命令: {command}")
                return False
            
            return True
            
        except Exception as e:
            logger.error(f"[EyeCommand] 执行失败: command={command}, error={e}")
            import traceback
            logger.error(traceback.format_exc())
            return False
    
    async def _handle_cycle_lid_style(self, params: Dict[str, Any]) -> bool:
        """
        循环切换眼睑样式
        
        Args:
            params: 参数（预留）
            
        Returns:
            是否成功
        """
        try:
            # 初始化样式列表
            await self._init_eye_styles()
            
            if not self._lid_styles:
                logger.warning("[CycleLid] 无可用眼睑样式")
                return False
            
            # 循环到下一个样式
            self._current_lid_style_index = (self._current_lid_style_index + 1) % len(self._lid_styles)
            next_style = self._lid_styles[self._current_lid_style_index]
            
            logger.info(f"[CycleLid] 切换到样式: {next_style} ({self._current_lid_style_index + 1}/{len(self._lid_styles)})")
            
            # 设置样式
            eye_interface = self.animation_manager.eye_interface
            await eye_interface.set_lid(side_id=next_style)
            
            # TODO: 可选 TTS 提示
            # await self._tts(f"切换到{next_style}眼型")
            
            return True
            
        except Exception as e:
            logger.error(f"[CycleLid] 失败: {e}")
            return False
    
    async def _handle_cycle_iris_style(self, params: Dict[str, Any]) -> bool:
        """
        循环切换虹膜样式
        
        Args:
            params: 参数
                - focus: 'pupil' 表示只循环瞳孔样式（可选）
                
        Returns:
            是否成功
        """
        try:
            # 初始化样式列表
            await self._init_eye_styles()
            
            if not self._iris_themes:
                logger.warning("[CycleIris] 无可用虹膜主题")
                return False
            
            # 获取主题列表
            theme_names = list(self._iris_themes.keys())
            
            # 循环到下一个主题
            self._current_iris_theme_index = (self._current_iris_theme_index + 1) % len(theme_names)
            current_theme = theme_names[self._current_iris_theme_index]
            
            # 获取主题下的样式
            styles = self._iris_themes[current_theme]
            if not styles:
                logger.warning(f"[CycleIris] 主题 {current_theme} 无可用样式")
                return False
            
            # 使用第一个样式（或循环样式）
            current_style = styles[0]
            
            logger.info(f"[CycleIris] 切换到: {current_theme}/{current_style} ({self._current_iris_theme_index + 1}/{len(theme_names)})")
            
            # 设置虹膜
            eye_interface = self.animation_manager.eye_interface
            await eye_interface.set_iris(theme=current_theme, style=current_style)
            
            # TODO: 可选 TTS 提示
            # await self._tts(f"切换到{current_theme}虹膜")
            
            return True
            
        except Exception as e:
            logger.error(f"[CycleIris] 失败: {e}")
            return False

