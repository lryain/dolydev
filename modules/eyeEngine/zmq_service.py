"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

#!/usr/bin/env python3
"""
EyeEngine ZMQ 服务

基于 ZMQ 提供 eyeEngine 的所有功能控制接口
支持任务优先级管理和事件发布

协议：
- 命令通道：REQ-REP 模式（客户端发送命令，服务器返回响应）
- 事件通道：PUB-SUB 模式（服务器发布事件，客户端订阅）
"""

import sys
import os
import time
import logging
import argparse
import signal
import json
import threading
from pathlib import Path
from typing import Optional, Dict, Any

# 添加 libs 目录到路径
# 原来添加到三层父目录，改为添加到 'libs' 目录以确保 'eyeEngine' 包可导入
sys.path.insert(0, str(Path(__file__).parent.parent))

import zmq

from eyeEngine.engine import EyeEngine
from eyeEngine.config import EngineConfig, LcdSide
from eyeEngine.config_loader_v2 import get_config_loader
from eyeEngine.task_priority import TaskPriorityManager, TaskPriority
from eyeEngine.constants import MIN_FPS, MAX_FPS
from eyeEngine.visibility_manager import EyeVisibilityManager

logging.basicConfig(
    level=logging.DEBUG,  # ★ 改为 DEBUG 以捕捉事件发布日志
    format='[%(asctime)s] [%(levelname)s] %(name)s: %(message)s'
)
logger = logging.getLogger(__name__)
logger.setLevel(logging.DEBUG)  # ★ 确保 logger 本身也是 DEBUG


class EyeEngineZmqService:

    def _cmd_play_sprite_animation(self, cmd: Dict, priority: int) -> Dict:
        """播放精灵动画（SpriteAnimation），支持 loop/loop_count/speed/clear_time/side"""
        category = cmd.get('category')
        animation = cmd.get('animation')
        start = int(cmd.get('start', 0))
        loop = cmd.get('loop', False)
        loop_count = cmd.get('loop_count')
        speed = float(cmd.get('speed', 1.0))
        clear_time = int(cmd.get('clear_time', 0))
        side = cmd.get('side', 'BOTH')
        if not category or not animation:
            return {"success": False, "error": "缺少 category 或 animation 参数"}
        try:
            logger.info(f"[ZMQ] 提交 sprite_animation: category={category}, animation={animation}, start={start}, loop={loop}, loop_count={loop_count}, speed={speed}, clear_time={clear_time}, side={side}, priority={priority}, 当前任务: {self.task_manager.get_current_task_id()} 优先级: {self.task_manager.get_current_priority()}")
            overlay_id = self.engine.play_sprite_animation(category, animation, start=start, loop=loop, loop_count=loop_count, speed=speed, clear_time=clear_time, side=side)
            if not overlay_id:
                logger.warning(f"[ZMQ] sprite_animation 启动失败: category={category}, animation={animation}, 当前任务: {self.task_manager.get_current_task_id()} 优先级: {self.task_manager.get_current_priority()}")
                return {"success": False, "error": "启动失败"}
            self._publish_event("sprite_animation.started", {"overlay_id": overlay_id, "category": category, "animation": animation})
            logger.info(f"[ZMQ] sprite_animation 启动成功: overlay_id={overlay_id}, 当前任务: {self.task_manager.get_current_task_id()} 优先级: {self.task_manager.get_current_priority()}")
            return {"success": True, "overlay_id": overlay_id}
        except Exception as e:
            logger.error(f"_cmd_play_sprite_animation 异常: {e}, 当前任务: {self.task_manager.get_current_task_id()} 优先级: {self.task_manager.get_current_priority()}")
            return {"success": False, "error": str(e)}

    def _cmd_play_overlay_image(self, cmd: Dict, priority: int) -> Dict:
        """异步播放 overlay 图片（不阻塞 caller）"""
        image = cmd.get('image')
        fps = cmd.get('fps')
        loop = cmd.get('loop', False)
        side = cmd.get('side', 'BOTH')
        speed = float(cmd.get('speed', 1.0))
        delay_ms = int(cmd.get('delay_ms', 0))
        suspend_when_animating = bool(cmd.get('suspend_when_animating', False))

        if not image:
            return {"success": False, "error": "缺少 image 名称"}

        try:
            lcd_side = LcdSide[side.upper()] if side.upper() in ['LEFT', 'RIGHT', 'BOTH'] else LcdSide.BOTH
        except Exception:
            lcd_side = LcdSide.BOTH

        def bg_task():
            try:
                if delay_ms > 0:
                    time.sleep(delay_ms / 1000.0)
                overlay_id = self.engine.play_overlay_image(
                    image,
                    side=lcd_side,
                    loop=loop,
                    fps=fps,
                    speed=speed,
                    suspend_when_animating=suspend_when_animating,
                )
                if not overlay_id:
                    logger.error(f"play_overlay_image: 启动失败: {image}")
                    return
                # 发布事件
                try:
                    self._publish_event("overlay_image.started", {"overlay_id": overlay_id, "image": image})
                except Exception:
                    pass
            except Exception as e:
                logger.error(f"play_overlay_image 异常: {e}")

        t = threading.Thread(target=bg_task, daemon=True)
        t.start()

        return {"success": True, "task_id": f"play_overlay_image_{image}"}

    def _cmd_stop_overlay_image_sync(self, cmd: Dict, priority: int) -> Dict:
        """同步停止 overlay 图片（与 play_overlay_image_sync 配套）"""
        overlay_id = cmd.get('overlay_id')
    def _cmd_stop_overlay_image_sync(self, cmd: Dict, priority: int) -> Dict:
        """同步停止 overlay 图片（与 play_overlay_image_sync 配套）"""
        overlay_id = cmd.get('overlay_id')
        if not overlay_id:
            return {"success": False, "error": "缺少 overlay_id"}

        def stop_cb():
            ok = self.engine.stop_overlay_sequence(overlay_id)
            if not ok:
                raise RuntimeError(f"stop_overlay_image failed for {overlay_id}")
            return True

        accepted, result = self.task_manager.submit_task_sync(f"stop_overlay_image_sync_{overlay_id}", stop_cb, priority=priority)
        if not accepted:
            return self._priority_reject()
        if isinstance(result, str) and result.startswith('Traceback'):
            return {"success": False, "error": result}
        try:
            self._publish_event('overlay.stopped', {"overlay_id": overlay_id})
        except Exception:
            logger.exception("_cmd_stop_overlay_image_sync: unable to publish overlay.stopped event")
        return {"success": True, "stopped": True}

    def _cmd_stop_overlay_image(self, cmd: Dict, priority: int) -> Dict:
        """停止 overlay 图片（异步）"""
        overlay_id = cmd.get('overlay_id')
        if not overlay_id:
            return {"success": False, "error": "缺少 overlay_id"}

        def task():
            ok = self.engine.stop_overlay_sequence(overlay_id)
            if ok:
                self._publish_event('overlay.stopped', {"overlay_id": overlay_id})
            else:
                raise RuntimeError(f"stop_overlay_image failed for {overlay_id}")

        task_id = f"stop_overlay_image_{overlay_id}"
        accepted = self.task_manager.submit_task(task_id, task, priority, blocking=False)
        if not accepted:
            return self._priority_reject()
        return {"success": True, "task_id": task_id}
        if not overlay_id:
            return {"success": False, "error": "缺少 overlay_id"}

        def stop_cb():
            ok = self.engine.stop_overlay_sequence(overlay_id)
            if not ok:
                raise RuntimeError(f"stop_overlay_image failed for {overlay_id}")
            return True

            accepted, result = self.task_manager.submit_task_sync(f"stop_overlay_image_sync_{overlay_id}", stop_cb, priority=priority)
            if not accepted:
                return self._priority_reject()
            if isinstance(result, str) and result.startswith('Traceback'):
                return {"success": False, "error": result}
            try:
                self._publish_event('overlay.stopped', {"overlay_id": overlay_id})
            except Exception:
                logger.exception("_cmd_stop_overlay_image_sync: unable to publish overlay.stopped event")
            return {"success": True, "stopped": True}

        def _cmd_stop_overlay_image(self, cmd: Dict, priority: int) -> Dict:
            """停止 overlay 图片（异步）"""
            overlay_id = cmd.get('overlay_id')
            if not overlay_id:
                return {"success": False, "error": "缺少 overlay_id"}

            def task():
                ok = self.engine.stop_overlay_sequence(overlay_id)
                if ok:
                    self._publish_event('overlay.stopped', {"overlay_id": overlay_id})
                else:
                    raise RuntimeError(f"stop_overlay_image failed for {overlay_id}")

            task_id = f"stop_overlay_image_{overlay_id}"
            accepted = self.task_manager.submit_task(task_id, task, priority, blocking=False)
            if not accepted:
                return self._priority_reject()
            return {"success": True, "task_id": task_id}

    def _cmd_play_overlay_image_sync(self, cmd: Dict, priority: int) -> Dict:
        """同步播放 overlay 图片并返回 overlay_id（尊重优先级检查）"""
        image = cmd.get('image')
        fps = cmd.get('fps')
        loop = cmd.get('loop', False)
        delay_ms = int(cmd.get('delay_ms', 0))
        side = cmd.get('side', 'BOTH')
        speed = float(cmd.get('speed', 1.0))
        suspend_when_animating = bool(cmd.get('suspend_when_animating', False))

        if not image:
            return {"success": False, "error": "缺少 image 名称"}

        try:
            lcd_side = LcdSide[side.upper()] if side.upper() in ['LEFT', 'RIGHT', 'BOTH'] else LcdSide.BOTH
        except Exception:
            lcd_side = LcdSide.BOTH

        def play_cb():
            if delay_ms > 0:
                time.sleep(delay_ms / 1000.0)
            return self.engine.play_overlay_image(
                image,
                side=lcd_side,
                loop=loop,
                fps=fps,
                speed=speed,
                suspend_when_animating=suspend_when_animating,
            )

        accepted, result = self.task_manager.submit_task_sync(f"play_overlay_image_sync_{image}", play_cb, priority=priority)
        if not accepted:
            return self._priority_reject()
        # result is overlay_id or traceback string
        if isinstance(result, str) and result.startswith('Traceback'):
            return {"success": False, "error": result}
        # 发布事件
        try:
            self._publish_event("overlay_image.started", {"overlay_id": result, "image": image})
        except Exception:
            pass
        return {"success": True, "overlay_id": result}
    """EyeEngine ZMQ 服务"""
    
    def __init__(self, config_file: Optional[str] = None, use_mock: bool = False):
        """
        初始化服务
        
        Args:
            config_file: 用户配置文件路径
            use_mock: 是否使用模拟驱动
        """
        # 加载配置
        self.config_loader = get_config_loader(user_config=config_file)
        self.cfg = self.config_loader.config
        
        # 初始化 EyeEngine
        engine_config = EngineConfig()
        engine_config.use_mock = use_mock or self.cfg['performance']['use_mock']
        engine_config.default_fps = self.cfg['performance']['default_fps']
        engine_config.default_fps_seq = self.cfg['performance'].get('default_fps_seq', 15)
        
        # 被动模式 (如果启用，则禁用自动行为如自动眨眼、轮播、默认表情)
        passive_cfg = self.cfg.get('passive_mode', {})
        if isinstance(passive_cfg, dict):
            passive_enabled = bool(passive_cfg.get('enabled', False))
        else:
            passive_enabled = bool(passive_cfg)
        engine_config.passive_mode = passive_enabled

        # 视频流配置（FaceReco 推流）
        video_cfg = self.cfg.get('video_stream', {}) or {}
        engine_config.video_stream_enabled = bool(video_cfg.get('enabled', False))
        engine_config.video_stream_resource_id = str(video_cfg.get('resource_id', 'facereco_video'))
        engine_config.video_stream_instance_id = int(video_cfg.get('instance_id', 0))
        engine_config.video_stream_target_lcd = str(video_cfg.get('target_lcd', 'RIGHT'))
        engine_config.video_stream_fps = int(video_cfg.get('fps', 15))
        engine_config.video_stream_timeout_ms = int(video_cfg.get('timeout_ms', 100))
        engine_config.video_stream_display_mode = str(video_cfg.get('display_mode', 'overlay'))
        engine_config.video_stream_overlay_style = str(video_cfg.get('overlay_style', 'full'))
        engine_config.video_stream_pupil_radius_ratio = float(video_cfg.get('pupil_radius_ratio', 0.35))

        # 自动复位配置
        auto_reset_cfg = self.cfg.get('auto_reset', {}) or {}
        engine_config.auto_reset_enabled = bool(auto_reset_cfg.get('enabled', True))
        engine_config.auto_reset_delay_ms = int(auto_reset_cfg.get('delay_ms', 300) or 0)
        engine_config.auto_reset_expression = auto_reset_cfg.get('expression', '') or ''
        engine_config.auto_reset_restore_passive = bool(auto_reset_cfg.get('restore_passive', True))
        engine_config.auto_reset_background = bool(auto_reset_cfg.get('auto_reset_background', True))

        # 自动眨眼配置 (如果未启用被动模式)
        blink_cfg = self.cfg.get('auto_blink', {})
        if not passive_enabled and blink_cfg:
            engine_config.auto_blink = blink_cfg.get('enabled', True)
            engine_config.blink_interval = (
                blink_cfg.get('interval_min', 2.0),
                blink_cfg.get('interval_max', 6.0)
            )
            engine_config.blink_animations = blink_cfg.get('animations', {})
        else:
            # 在被动模式下，确保不启用自动眨眼
            engine_config.auto_blink = False
            engine_config.blink_animations = {}
        
        self.engine = EyeEngine(config=engine_config)
        self.engine.init(use_mock=engine_config.use_mock)
        self._video_stream_manual_override = False
        
        # ★ 新增：注入 overlay 事件发布回调到 engine
        self.engine._on_overlay_event = self._publish_overlay_event
        
        # 任务优先级管理器 (必须在 _apply_default_config 之前，因为可能提交任务)
        task_cfg = self.cfg['task_priority']
        # honor default priority_enabled 配置，禁用时新任务会直接打断旧任务，不再出现“优先级不足”拒绝
        self.task_manager = TaskPriorityManager(
            max_priority=task_cfg['max_priority'],
            min_priority=task_cfg['min_priority'],
            default_priority=task_cfg['default_priority'],
            enabled=task_cfg.get('enabled', True)
        )
        self.task_manager.set_callbacks(
            on_complete=self._on_task_complete,
            on_interrupted=self._on_task_interrupted,
            on_failed=self._on_task_failed
        )
        logger.info(f"任务优先级开关初始化为: {'enabled' if self.task_manager.enabled else 'disabled'}")

        # 运行标志与轮播事件 (必须在 _apply_default_config 之前)
        self.running = False
        self.carousel_thread: Optional[threading.Thread] = None
        self.carousel_stop_event = threading.Event()

        # 应用默认配置
        self._apply_default_config()
        
        # ZMQ 上下文
        self.zmq_ctx = zmq.Context()
        
        # 命令接收套接字 (REP)
        zmq_cfg = self.cfg['zmq_service']
        self.cmd_socket = self.zmq_ctx.socket(zmq.REP)
        self.cmd_endpoint = zmq_cfg['command_endpoint']
        self.cmd_socket.bind(self.cmd_endpoint)
        logger.info(f"命令端点: {self.cmd_endpoint}")
        
        # 事件发布套接字 (PUB)
        self.event_socket = self.zmq_ctx.socket(zmq.PUB)
        self.event_endpoint = zmq_cfg['event_endpoint']
        self.event_socket.bind(self.event_endpoint)
        logger.info(f"事件端点: {self.event_endpoint}")
        
        # ★ 初始化 Eye 显示/隐藏管理器
        self.visibility_manager = EyeVisibilityManager(
            zmq_publisher=self._publish_event
        )
        logger.info("EyeVisibilityManager 已初始化")
        
        # ★ Widget 事件订阅 (用于 LCD 互斥)
        self._widget_sub_socket = None
        self._widget_listener_thread = None
        self._widget_listener_running = False
        widget_pub_endpoint = zmq_cfg.get('widget_pub_endpoint', 'ipc:///tmp/doly_widget_pub.sock')
        self._setup_widget_listener(widget_pub_endpoint)
        
        # ★ Vision 事件订阅 (用于人脸消失清空流等)
        self._vision_sub_socket = None
        self._vision_listener_thread = None
        self._vision_listener_running = False
        vision_pub_endpoint = zmq_cfg.get('vision_pub_endpoint', 'ipc:///tmp/doly_vision_events.sock')
        self._setup_vision_listener(vision_pub_endpoint)
    
    def _setup_widget_listener(self, endpoint: str):
        """设置 widget 事件监听器"""
        try:
            self._widget_sub_socket = self.zmq_ctx.socket(zmq.SUB)
            self._widget_sub_socket.connect(endpoint)
            # 订阅 widget LCD 相关事件
            self._widget_sub_socket.setsockopt_string(zmq.SUBSCRIBE, "event.widget.lcd_request")
            self._widget_sub_socket.setsockopt_string(zmq.SUBSCRIBE, "event.widget.lcd_released")
            logger.info(f"Widget 事件订阅端点: {endpoint}")
            
            # 启动监听线程
            self._widget_listener_running = True
            self._widget_listener_thread = threading.Thread(
                target=self._widget_listener_loop,
                name="WidgetListener",
                daemon=True
            )
            self._widget_listener_thread.start()
            logger.info("Widget 事件监听线程已启动")
        except Exception as e:
            logger.warning(f"设置 Widget 事件监听失败 (widget_service 可能未运行): {e}")
    
    def _widget_listener_loop(self):
        """Widget 事件监听循环"""
        logger.info("[WidgetListener] 监听循环启动")
        while self._widget_listener_running:
            try:
                if self._widget_sub_socket.poll(500):  # 500ms 超时
                    # 接收多部分消息 [topic, data]
                    parts = self._widget_sub_socket.recv_multipart()
                    if len(parts) >= 2:
                        topic = parts[0].decode('utf-8')
                        data_str = parts[1].decode('utf-8')
                        try:
                            data = json.loads(data_str)
                        except json.JSONDecodeError:
                            data = {}
                        
                        self._handle_widget_event(topic, data)
            except zmq.ZMQError as e:
                if e.errno == zmq.ETERM:
                    break
                logger.warning(f"[WidgetListener] ZMQ 错误: {e}")
            except Exception as e:
                logger.error(f"[WidgetListener] 处理事件异常: {e}", exc_info=True)
        
        logger.info("[WidgetListener] 监听循环退出")
    
    def _setup_vision_listener(self, endpoint: str):
        """设置 Vision 事件监听器"""
        try:
            self._vision_sub_socket = self.zmq_ctx.socket(zmq.SUB)
            self._vision_sub_socket.connect(endpoint)
            # 订阅 Vision 相关事件（注意主题是 event.vision.face.lost）
            print(f"-------------> Connecting to Vision event endpoint: {endpoint}")
            self._vision_sub_socket.setsockopt_string(zmq.SUBSCRIBE, "event.vision")
            logger.info(f"Vision 事件订阅端点: {endpoint}")
            
            # 启动监听线程
            self._vision_listener_running = True
            self._vision_listener_thread = threading.Thread(
                target=self._vision_listener_loop,
                name="VisionListener",
                daemon=True
            )
            self._vision_listener_thread.start()
            logger.info("Vision 事件监听线程已启动")
        except Exception as e:
            logger.warning(f"设置 Vision 事件监听失败 (FaceReco 可能未运行): {e}")
    
    def _vision_listener_loop(self):
        """Vision 事件监听循环"""
        logger.info("[VisionListener] 监听循环启动")
        receive_count = 0
        poll_count = 0
        while self._vision_listener_running:
            try:
                # poll_count += 1
                # if poll_count % 10 == 0:
                #     logger.debug(f"[VisionListener] 轮询中... (poll_count={poll_count})")
                
                if self._vision_sub_socket.poll(500):  # 500ms 超时
                    # 接收多部分消息 [topic, data]
                    parts = self._vision_sub_socket.recv_multipart()
                    receive_count += 1
                    logger.info(f"[VisionListener] 🎉 收到消息 #{receive_count}: parts={len(parts)}")
                    
                    if len(parts) >= 2:
                        topic = parts[0].decode('utf-8')
                        data_str = parts[1].decode('utf-8')
                        logger.info(f"[VisionListener] 📨 主题={topic}, 数据长度={len(data_str)}")
                        
                        try:
                            data = json.loads(data_str)
                        except json.JSONDecodeError as e:
                            logger.error(f"[VisionListener] JSON 解析失败: {e}")
                            data = {}
                        
                        self._handle_vision_event(topic, data)
                    else:
                        logger.warning(f"[VisionListener] 消息部分数不足: {len(parts)}")
            except zmq.ZMQError as e:
                if e.errno == zmq.ETERM:
                    break
                logger.warning(f"[VisionListener] ZMQ 错误: {e}")
            except Exception as e:
                logger.error(f"[VisionListener] 处理事件异常: {e}", exc_info=True)
        
        logger.info("[VisionListener] 监听循环退出 (poll_count={poll_count}, receive_count={receive_count})")
    
    def _handle_vision_event(self, topic: str, data: Dict):
        """处理 Vision 事件"""
        try:
            logger.info(f"[VisionEvent] 处理事件: topic={topic}")
            logger.debug(f"[VisionEvent] 完整数据: {data}")

            if self._video_stream_manual_override:
                logger.debug("[VisionEvent] 视频流处于手动强制显示模式，忽略人脸事件门控")
                return
            
            if topic == "event.vision.face":
                # 人脸出现/位置更新：激活视频流消费
                if hasattr(self.engine, "set_video_stream_visibility"):
                    self.engine.set_video_stream_visibility(True, refresh=False)
                elif hasattr(self.engine, "set_video_stream_active"):
                    self.engine.set_video_stream_active(True)
                
            elif topic == "event.vision.face.lost":
                # 人脸消失：停用视频流并清空上一帧缓存，恢复默认眼睛渲染
                event_data = data.get("data", data)
                
                tracker_id = event_data.get("id")
                lost_frames = event_data.get("lost_frames")
                logger.info(f"[VisionEvent] 🔴 人脸消失事件: tracker_id={tracker_id}, lost_frames={lost_frames}")

                if hasattr(self.engine, "set_video_stream_visibility"):
                    self.engine.set_video_stream_visibility(False, clear_cached_frame=True, refresh=True)
                elif hasattr(self.engine, "set_video_stream_active"):
                    self.engine.set_video_stream_active(False)
            # else:
            #     logger.warning(f"[VisionEvent] 未知事件主题: {topic}")
                
        except Exception as e:
            logger.error(f"处理 Vision 事件失败: topic={topic}, error={e}", exc_info=True)
    
    def _handle_widget_event(self, topic: str, data: Dict):
        """处理 widget 事件"""
        logger.info(f"[WidgetListener] 收到事件: {topic}, data={data}")
        
        if topic == "event.widget.lcd_request":
            # Widget 请求 LCD 控制权，暂停 eyeEngine 渲染
            logger.info("[WidgetListener] Widget 请求 LCD 控制权，暂停渲染...")
            widget_id = data.get("widget_id", "unknown")
            
            # 更新 visibility manager 状态
            # 这里假设 widget_id 就是 widget 名称
            timeout = data.get("timeout")  # 可选
            self.visibility_manager.show_widget(widget_id, timeout)
            
            if self.engine.pause_lcd():
                logger.info(f"[WidgetListener] LCD 已暂停，让渡给 widget: {widget_id}")
                self._publish_event("eye.lcd_paused", {
                    "reason": "widget_request",
                    "widget_id": widget_id
                })
            else:
                logger.error("[WidgetListener] 暂停 LCD 失败")
        
        elif topic == "event.widget.lcd_released":
            # Widget 释放 LCD 控制权，恢复 eyeEngine 渲染
            logger.info("[WidgetListener] Widget 释放 LCD 控制权，恢复渲染...")
            widget_id = data.get("widget_id", "unknown")
            
            # 更新 visibility manager 状态
            self.visibility_manager.restore_eye(reason="widget_released")
            
            if self.engine.resume_lcd():
                logger.info(f"[WidgetListener] LCD 已恢复，widget {widget_id} 已退出")
                self._publish_event("eye.lcd_resumed", {
                    "reason": "widget_released",
                    "widget_id": widget_id
                })
            else:
                logger.error("[WidgetListener] 恢复 LCD 失败")
    
    def handle_external_event(self, event_name: str) -> bool:
        """
        处理外部事件，判断是否需要强制恢复 eye 显示
        
        这个方法应该被外部模块（如 daemon.py）调用，
        当接收到特定事件时通知 eyeEngine 进行处理
        
        Args:
            event_name: 事件名称（如 "event.audio.wakeup_detected"）
        
        Returns:
            是否触发了强制恢复
        """
        forced_restore = self.visibility_manager.handle_event(event_name)
        
        if forced_restore:
            # 强制恢复被触发，需要恢复 LCD 显示
            if self.engine.resume_lcd():
                logger.info(f"事件 {event_name} 强制恢复 eye 显示成功")
            else:
                logger.warning(f"事件 {event_name} 强制恢复 eye 显示失败")
        
        return forced_restore
    
    def _apply_default_config(self):
        """应用默认配置到引擎"""
        app_cfg = self.cfg['appearance']
        passive = bool(getattr(self.engine._config, 'passive_mode', False))
        if passive:
            logger.info("被动模式启用：将跳过自动行为（auto_blink, default_expression, expression_carousel）")

        # 设置虹膜
        iris_theme = app_cfg['iris_theme']
        iris_style = app_cfg['iris_style']
        if iris_theme and iris_style:
            self.engine.set_iris(iris_theme, iris_style)

        # 左右眼虹膜
        if app_cfg.get('left_iris_theme') and app_cfg.get('left_iris_style'):
            self.engine.controller.set_iris_theme(
                app_cfg['left_iris_theme'],
                app_cfg['left_iris_style'],
                LcdSide.LEFT
            )

        if app_cfg.get('right_iris_theme') and app_cfg.get('right_iris_style'):
            self.engine.controller.set_iris_theme(
                app_cfg['right_iris_theme'],
                app_cfg['right_iris_style'],
                LcdSide.RIGHT
            )

        # 设置眼睑
        if app_cfg.get('side_lid_id'):
            self.engine.controller.set_lid(side_id=app_cfg['side_lid_id'])
        if app_cfg.get('top_lid_id'):
            self.engine.controller.set_lid(top_id=app_cfg['top_lid_id'])
        if app_cfg.get('bottom_lid_id'):
            self.engine.controller.set_lid(bottom_id=app_cfg['bottom_lid_id'])

        # 左右眼睑
        if app_cfg.get('left_lid_id') is not None:
            self.engine.controller.set_lid(side_id=app_cfg['left_lid_id'], side=LcdSide.LEFT)
        if app_cfg.get('right_lid_id') is not None:
            self.engine.controller.set_lid(side_id=app_cfg['right_lid_id'], side=LcdSide.RIGHT)

        # 设置背景
        if app_cfg.get('background_type') and app_cfg.get('background_style'):
            # 保存默认背景配置以便超时恢复
            self.engine.controller.set_default_background(
                app_cfg['background_type'],
                app_cfg['background_style']
            )
            self.engine.controller.set_background(
                app_cfg['background_style'],
                app_cfg['background_type']
            )

        # 设置亮度
        brightness = app_cfg.get('brightness', 8)
        self.engine.controller.set_brightness(brightness)

        if app_cfg.get('left_brightness') is not None:
            self.engine.controller.set_brightness(app_cfg['left_brightness'], LcdSide.LEFT)
        if app_cfg.get('right_brightness') is not None:
            self.engine.controller.set_brightness(app_cfg['right_brightness'], LcdSide.RIGHT)

        # 自动眨眼
        blink_cfg = self.cfg['auto_blink']
        if not passive and blink_cfg['enabled']:
            interval = (blink_cfg['interval_min'], blink_cfg['interval_max'])
            self.engine.controller.enable_auto_blink(interval)
        else:
            logger.debug("跳过自动眨眼（被动模式或配置禁用）")

        # 默认表情
        default_expr = self.cfg.get('default_expression')
        if default_expr and not passive:
            self.engine.play_behavior(default_expr, blocking=False)
        elif default_expr and passive:
            logger.debug("跳过默认表情设置（被动模式）")

        # 自动表情轮播
        carousel_cfg = self.cfg['auto_expression_carousel']
        if carousel_cfg['enabled'] and not passive:
            self._start_expression_carousel()
        else:
            logger.debug("跳过自动表情轮播（被动模式或配置禁用）")

        # 捕获初始基线状态用于自动复位
        try:
            self.engine.capture_baseline_state()
        except Exception:
            logger.exception("初始化捕获基线状态失败")
    
    def _start_expression_carousel(self):
        """启动表情轮播"""
        carousel_cfg = self.cfg['auto_expression_carousel']
        
        def carousel_loop():
            import random
            expressions = carousel_cfg['expressions']
            duration = carousel_cfg['duration']
            interval = carousel_cfg['interval']
            random_order = carousel_cfg['random_order']
            
            idx = 0
            while not self.carousel_stop_event.is_set():
                if not expressions:
                    time.sleep(1)
                    continue
                
                if random_order:
                    expr = random.choice(expressions)
                else:
                    expr = expressions[idx % len(expressions)]
                    idx += 1
                
                # 使用低优先级播放
                self.task_manager.submit_task(
                    task_id=f"carousel_{expr}",
                    callback=lambda e=expr: self.engine.play_behavior(e, blocking=True),
                    priority=TaskPriority.LOW,
                    blocking=False
                )
                
                # 等待时长 + 间隔
                total_wait = duration + interval
                self.carousel_stop_event.wait(total_wait)
        
        self.carousel_thread = threading.Thread(target=carousel_loop, daemon=True)
        self.carousel_thread.start()
        logger.info("自动表情轮播已启动")
    
    def _on_task_complete(self, task_id: str):
        """任务完成回调"""
        # 附带播放相关的 FPS 指标（如果可用）
        fps = None
        last_avg = None
        try:
            player = getattr(self.engine, 'eye_animation_player', None)
            if player:
                fps = float(player.current_fps)
                last_avg = float(player.last_average_fps)
        except Exception:
            pass
        logger.info(f"[ZMQ] 任务完成: {task_id}, 当前任务: {self.task_manager.get_current_task_id()} 优先级: {self.task_manager.get_current_priority()}")
        self._publish_event("task.complete", {"task_id": task_id, "current_fps": fps, "last_average_fps": last_avg})
        self._publish_priority_snapshot()
    
    def _on_task_failed(self, task_id: str, tb: str):
        """任务失败回调"""
        logger.error(f"[ZMQ] 任务失败: {task_id}, 当前任务: {self.task_manager.get_current_task_id()} 优先级: {self.task_manager.get_current_priority()}\ntraceback: {tb}")
        self._publish_event("task.failed", {"task_id": task_id, "traceback": tb})
        self._publish_priority_snapshot()
    
    def _on_task_interrupted(self, old_task_id: str, new_task_id: str):
        """任务被打断回调"""
        logger.warning(f"[ZMQ] 任务被打断: {old_task_id} -> {new_task_id}, 当前任务: {self.task_manager.get_current_task_id()} 优先级: {self.task_manager.get_current_priority()}")
        self._publish_event("task.interrupted", {
            "old_task_id": old_task_id,
            "new_task_id": new_task_id
        })
        self._publish_priority_snapshot()
    
    def _publish_event(self, event_type: str, data: Dict[str, Any]):
        """发布事件"""
        try:
            import json
            msg = {
                "type": event_type,
                "timestamp": time.time(),
                **data
            }
            # logger.debug(f"发布事件: {event_type} - {data}")
            # ★ 使用 send_multipart 而不是 send_json，以确保主题被正确发送
            # ZMQ PUB/SUB 模式需要第一帧为主题，第二帧为数据
            self.event_socket.send_multipart([
                event_type.encode('utf-8'),
                json.dumps(msg).encode('utf-8')
            ])
        except Exception as e:
            logger.error(f"发布事件失败: {e}")

    def _publish_overlay_event(self, event_type: str, data: Dict[str, Any]):
        """发布 overlay 事件的回调（由 engine 调用）"""
        try:
            self._publish_event(event_type, data)
            logger.info(f"[EyeEngine回调] 发布 {event_type} 事件: {data}")
        except Exception as e:
            logger.error(f"[EyeEngine回调] 发布 {event_type} 事件失败: {e}")

    def _publish_priority_snapshot(self):
        """发布当前优先级占用/队列快照"""
        try:
            snapshot = self.task_manager.get_snapshot()
            self._publish_event("priority.snapshot", {"snapshot": snapshot})
        except Exception:
            logger.exception("发布优先级快照失败")

    def _priority_reject(self, message: str = "任务被拒绝（优先级不足）") -> Dict[str, Any]:
        snap = {}
        try:
            snap = self.task_manager.get_snapshot()
        except Exception:
            logger.exception("获取优先级快照失败")
        holder = snap.get("current") if isinstance(snap, dict) else None
        queue = snap.get("pending") if isinstance(snap, dict) else None
        return {
            "success": False,
            "error": message,
            "holder": holder,
            "queue": queue
        }
    
    def handle_command(self, cmd: Dict[str, Any]) -> Dict[str, Any]:
        """
        处理命令
        
        Args:
            cmd: 命令字典
        
        Returns:
            响应字典
        """
        action = cmd.get('action')
        logger.debug(f"Handling command: {cmd}")
        if not action:
            return {"success": False, "error": "缺少 action 字段"}

        try:
            # 获取优先级
            priority = cmd.get('priority', self.task_manager.default_priority)

            # 分发命令
            # Log state-changing commands at INFO level to trace unexpected resets
            state_changing = {'set_iris','set_lid','set_background','set_brightness','play_animation','play_behavior','play_category','stop'}
            if action in state_changing:
                logger.info(f"收到控制命令: action={action}, payload={cmd}")

            if action == "ping":
                resp = {"success": True, "message": "pong"}
                logger.debug(f"Response: {resp}")
                return resp

            elif action == "set_iris":
                return self._cmd_set_iris(cmd, priority)

            elif action == "set_lid":
                return self._cmd_set_lid(cmd, priority)

            elif action == "set_background":
                return self._cmd_set_background(cmd, priority)

            elif action == "set_brightness":
                return self._cmd_set_brightness(cmd, priority)

            elif action == "play_animation":
                return self._cmd_play_animation(cmd, priority)

            elif action == "play_behavior":
                return self._cmd_play_behavior(cmd, priority)

            elif action == "play_category":
                return self._cmd_play_category(cmd, priority)

            elif action == "play_all_animations":
                return self._cmd_play_all_animations(cmd, priority)

            elif action == "play_sequence":
                return self._cmd_play_sequence(cmd, priority)

            elif action == "play_sequence_animations":
                # backward-compatible async task submission
                return self._cmd_play_overlay_sequence(cmd, priority)

            elif action == "play_sprite_animation":
                return self._cmd_play_sprite_animation(cmd, priority)

            elif action == "play_overlay_sequence_sync":
                return self._cmd_play_overlay_sequence_sync(cmd, priority)

            elif action == "stop_overlay_sequence":
                return self._cmd_stop_overlay_sequence(cmd, priority)

            elif action == "stop_overlay_sequence_sync":
                return self._cmd_stop_overlay_sequence_sync(cmd, priority)

            elif action == "play_overlay_image":
                return self._cmd_play_overlay_image(cmd, priority)

            elif action == "play_overlay_image_sync":
                return self._cmd_play_overlay_image_sync(cmd, priority)

            elif action == "play_text_overlay":
                return self._cmd_play_text_overlay(cmd, priority)

            elif action == "stop_overlay_image":
                return self._cmd_stop_overlay_image(cmd, priority)

            elif action == "stop_overlay_image_sync":
                return self._cmd_stop_overlay_image_sync(cmd, priority)

            elif action == "blink":
                return self._cmd_blink(cmd, priority)

            elif action == "set_gaze":
                return self._cmd_set_gaze(cmd, priority)

            elif action == "stop":
                return self._cmd_stop()

            elif action == "set_priority_enabled":
                enabled = cmd.get('enabled', True)
                try:
                    # 优先使用管理器的 set_enabled，保持日志一致
                    setter = getattr(self.task_manager, "set_enabled", None)
                    if callable(setter):
                        setter(enabled)
                    else:
                        self.task_manager.enabled = enabled
                    logger.info(f"Priority system {'enabled' if enabled else 'disabled'}")
                    snap = self.task_manager.get_snapshot()
                    self._publish_priority_snapshot()
                    return {"success": True, "enabled": enabled, "status": snap}
                except Exception as e:
                    logger.error(f"设置优先级开关失败: {e}")

            elif action == "list_sequences":
                return self._cmd_list_sequences()

            elif action == "list_behaviors":
                return self._cmd_list_behaviors()

            elif action == "list_iris":
                return self._cmd_list_iris()

            elif action == "list_backgrounds":
                return self._cmd_list_backgrounds()

            elif action == "debug_fail":
                return self._cmd_debug_fail(cmd, priority)

            # ===== Eye Visibility Management =====
            elif action == "show_widget":
                return self._cmd_show_widget(cmd)
            
            elif action == "restore_eye":
                return self._cmd_restore_eye(cmd)
            
            elif action == "pause_auto_restore":
                return self._cmd_pause_auto_restore(cmd)
            
            elif action == "resume_auto_restore":
                return self._cmd_resume_auto_restore(cmd)
            
            elif action == "set_manual_mode":
                return self._cmd_set_manual_mode(cmd)
            
            elif action == "get_visibility_status":
                return self._cmd_get_visibility_status(cmd)

            # ===== Video Stream =====
            elif action == "enable_video_stream":
                return self._cmd_enable_video_stream(cmd)

            elif action == "disable_video_stream":
                return self._cmd_disable_video_stream(cmd)

            elif action == "video_stream_status":
                return self._cmd_video_stream_status(cmd)
            
            # ===== End Visibility Management =====

            else:
                return {"success": False, "error": f"未知命令: {action}"}

        except Exception as e:
            logger.error(f"处理命令异常: {e}", exc_info=True)
            return {"success": False, "error": str(e)}
    
    def _cmd_set_iris(self, cmd: Dict, priority: int) -> Dict:
        """设置虹膜主题和样式（瞬时配置操作，不占据任务槽位）"""
        theme = cmd.get('theme')
        style = cmd.get('style')
        side = cmd.get('side', 'BOTH')

        if not theme or not style:
            return {"success": False, "error": "缺少 theme 或 style"}

        try:
            lcd_side = LcdSide[side.upper()] if side.upper() in ['LEFT', 'RIGHT', 'BOTH'] else LcdSide.BOTH
            self.engine.controller.set_iris_theme(theme, style, lcd_side)
            task_id = f"set_iris_{side}_{theme}_{style}"
            logger.info(f"[ZMQ] 虹膜设置完成（直接执行）: task_id={task_id}")
            return {"success": True, "task_id": task_id}
        except Exception as e:
            logger.error(f"[ZMQ] 虹膜设置失败: {e}")
            return {"success": False, "error": str(e)}

    def _cmd_set_lid(self, cmd: Dict, priority: int) -> Dict:
        """设置眼睑（瞬时配置操作，不占据任务槽位）"""
        side_id = cmd.get('side_id')
        top_id = cmd.get('top_id')
        bottom_id = cmd.get('bottom_id')
        side = cmd.get('side', 'BOTH')

        try:
            lcd_side = LcdSide[side.upper()] if side.upper() in ['LEFT', 'RIGHT', 'BOTH'] else LcdSide.BOTH
            self.engine.controller.set_lid(side_id=side_id, top_id=top_id, bottom_id=bottom_id, side=lcd_side)
            task_id = f"set_lid_{side}"
            logger.info(f"[ZMQ] 眼睑设置完成（直接执行）: task_id={task_id}")
            return {"success": True, "task_id": task_id}
        except Exception as e:
            logger.error(f"[ZMQ] 眼睑设置失败: {e}")
            return {"success": False, "error": str(e)}

    def _cmd_set_background(self, cmd: Dict, priority: int) -> Dict:
        """设置背景（瞬时配置操作，不占据任务槽位）"""
        style = cmd.get('style')
        bg_type = cmd.get('type', 'COLOR')
        side = cmd.get('side', 'BOTH')
        duration_ms = cmd.get('duration_ms', 0)

        if not style:
            return {"success": False, "error": "缺少 style"}

        try:
            lcd_side = LcdSide[side.upper()] if side.upper() in ['LEFT', 'RIGHT', 'BOTH'] else LcdSide.BOTH
            
            # 传递 duration_ms 参数
            duration_str = f", duration={duration_ms}ms" if duration_ms > 0 else " (permanent)"
            logger.info(f"[ZMQ] 设置背景: style={style}, type={bg_type}, side={side}{duration_str}")
            
            self.engine.controller.set_background(style, bg_type, lcd_side, duration_ms=duration_ms)
            task_id = f"set_background_{style}_{side}"
            logger.info(f"[ZMQ] 背景设置完成（直接执行）: task_id={task_id}")
            return {"success": True, "task_id": task_id}
        except Exception as e:
            logger.error(f"[ZMQ] 背景设置失败: {e}")
            return {"success": False, "error": str(e)}

    def _cmd_set_brightness(self, cmd: Dict, priority: int) -> Dict:
        """设置亮度（瞬时配置操作，不占据任务槽位）"""
        level = cmd.get('level')
        side = cmd.get('side', 'BOTH')

        if level is None:
            return {"success": False, "error": "缺少 level"}

        try:
            lcd_side = LcdSide[side.upper()] if side.upper() in ['LEFT', 'RIGHT', 'BOTH'] else LcdSide.BOTH
            self.engine.controller.set_brightness(level, lcd_side)
            task_id = f"set_brightness_{side}_{level}"
            logger.info(f"[ZMQ] 亮度设置完成（直接执行）: task_id={task_id}")
            return {"success": True, "task_id": task_id}
        except Exception as e:
            logger.error(f"[ZMQ] 亮度设置失败: {e}")
            return {"success": False, "error": str(e)}

    def _cmd_play_animation(self, cmd: Dict, priority: int) -> Dict:
        """播放动画"""
        logger.info(f"[_cmd_play_animation] 开始: animation={cmd.get('animation')}, priority={priority}")
        animation = cmd.get('animation')
        anim_id = cmd.get('id')
        fps = cmd.get('fps')
        hold_duration = cmd.get('hold_duration', 0)
        # 验证并限定 fps
        if fps is not None:
            try:
                fps = int(fps)
            except Exception:
                return {"success": False, "error": "invalid fps"}
            fps = max(MIN_FPS, min(MAX_FPS, fps))

        if not animation and anim_id is None:
            return {"success": False, "error": "缺少 animation 或 id"}

        def task():
            logger.info(f"[_cmd_play_animation task] 执行: animation={animation}, anim_id={anim_id}, hold_duration={hold_duration}")
            if anim_id is not None:
                self.engine.play_eye_animation_by_id(anim_id, fps=fps, blocking=True, hold_duration=hold_duration)
            else:
                self.engine.play_eye_animation(animation, fps=fps, blocking=True, hold_duration=hold_duration)
            logger.info(f"[_cmd_play_animation task] 完成: animation={animation}")

        task_id = f"play_anim_{animation or anim_id}"
        logger.info(f"[_cmd_play_animation] 提交任务: task_id={task_id}, priority={priority}")
        accepted = self.task_manager.submit_task(task_id, task, priority, blocking=False)
        logger.info(f"[_cmd_play_animation] 提交结果: task_id={task_id}, accepted={accepted}")
        if not accepted:
            return self._priority_reject()
        logger.info(f"[_cmd_play_animation] 返回: task_id={task_id}")
        return {"success": True, "task_id": task_id, "fps": fps}

    def _cmd_play_behavior(self, cmd: Dict, priority: int) -> Dict:
        """播放行为"""
        behavior = cmd.get('behavior')
        level = cmd.get('level', 1)
        fps = cmd.get('fps')
        hold_duration = cmd.get('hold_duration', 0)
        # 验证并限定 fps
        if fps is not None:
            try:
                fps = int(fps)
            except Exception:
                return {"success": False, "error": "invalid fps"}
            fps = max(MIN_FPS, min(MAX_FPS, fps))

        if not behavior:
            return {"success": False, "error": "缺少 behavior"}

        def task():
            self.engine.play_behavior(behavior, level=level, fps=fps, blocking=True, hold_duration=hold_duration)

        task_id = f"play_behavior_{behavior}"
        accepted = self.task_manager.submit_task(task_id, task, priority, blocking=False)
        if not accepted:
            return self._priority_reject()
        return {"success": True, "task_id": task_id, "fps": fps}

    def _cmd_play_category(self, cmd: Dict, priority: int) -> Dict:
        """播放指定分类的动画"""
        category = cmd.get('category')
        play_all = cmd.get('play_all', False)
        fps = cmd.get('fps')

        if not category:
            return {"success": False, "error": "缺少 category 名称"}

        def task():
            if play_all:
                anims = self.engine.asset_manager.get_animations_by_category(category)
                for anim in anims:
                    if self.task_manager.should_stop(): break
                    self.engine.play_eye_animation(category, anim, fps=fps, blocking=True)
            else:
                anim = self.engine.asset_manager.get_random_animation_for_category(category)
                if anim:
                    self.engine.play_eye_animation(category, anim, fps=fps, blocking=True)
                else:
                    logger.error(f"Category {category} has no animations")

        task_id = f"play_category_{category}"
        accepted = self.task_manager.submit_task(task_id, task, priority, blocking=False)
        if not accepted:
            return self._priority_reject()
        return {"success": True, "task_id": task_id}

    def _cmd_play_all_animations(self, cmd: Dict, priority: int) -> Dict:
        """播放所有 XML 动画"""
        fps = cmd.get('fps')
        
        def task():
            anims = self.engine.asset_manager.get_available_animations()
            for anim in sorted(anims):
                if self.task_manager.should_stop(): break
                # 注意：list 返回的是简单名，play_eye_animation 内部会查找
                self.engine.play_eye_animation(anim, fps=fps, blocking=True)

        task_id = "play_all_animations"
        accepted = self.task_manager.submit_task(task_id, task, priority, blocking=False)
        if not accepted:
            return self._priority_reject()
        return {"success": True, "task_id": task_id}

    def _cmd_play_sequence(self, cmd: Dict, priority: int) -> Dict:
        """播放 .seq 序列"""
        sequence = cmd.get('sequence')
        fps = cmd.get('fps')
        loop = cmd.get('loop', False)
        side = cmd.get('side', 'BOTH')

        if not sequence:
            return {"success": False, "error": "缺少 sequence 名称"}

        def task():
            lcd_side = LcdSide[side.upper()] if side.upper() in ['LEFT', 'RIGHT', 'BOTH'] else LcdSide.BOTH
            player = self.engine.play_sequence(sequence, side=lcd_side, loop=loop, fps=fps)
            if not player:
                raise RuntimeError(f"无法启动序列播放: {sequence}")
            
            # 阻塞等待直到播放完成（除非是循环播放且任务没停止）
            while player.is_playing and self.running:
                time.sleep(0.05)
                # 如果是循环播放，通常需要手动停止任务，否则它会一直运行直到被打断
                if self.task_manager.should_stop():
                    player.stop()
                    break

        task_id = f"play_seq_{sequence}"
        accepted = self.task_manager.submit_task(task_id, task, priority, blocking=False)
        if not accepted:
            return self._priority_reject()
        return {"success": True, "task_id": task_id}

    def _cmd_play_overlay_sequence(self, cmd: Dict, priority: int) -> Dict:
        """异步播放 overlay/sprite 动画，支持精灵动画参数，后台线程启动，详细日志"""
        # 兼容 sequence 或 sprite 两种参数
        sequence = cmd.get('sequence')
        sprite = cmd.get('sprite')
        fps = cmd.get('fps')
        loop = cmd.get('loop', False)
        loop_count = cmd.get('loop_count')
        side = cmd.get('side', 'BOTH')
        speed = float(cmd.get('speed', 1.0))
        x = cmd.get('x')
        y = cmd.get('y')
        scale = cmd.get('scale')
        rotation = cmd.get('rotation')
        duration_ms = cmd.get('duration_ms')
        clear_time = int(cmd.get('clear_time', 0))
        delay_ms = cmd.get('delay_ms', 0)
        exclusive = bool(cmd.get('exclusive', False))

        # 参数日志
        logger.info(f"[ZMQ] play_sequence_animations: sequence={sequence}, sprite={sprite}, side={side}, loop={loop}, loop_count={loop_count}, fps={fps}, speed={speed}, clear_time={clear_time}, exclusive={exclusive}, x={x}, y={y}, scale={scale}, rotation={rotation}, duration_ms={duration_ms}, delay_ms={delay_ms}")

        # 参数校验
        if not sequence and not sprite:
            logger.error("play_sequence_animations: 缺少 sequence 或 sprite 名称")
            return {"success": False, "error": "缺少 sequence 或 sprite 名称"}

        try:
            lcd_side = LcdSide[side.upper()] if side and side.upper() in ['LEFT', 'RIGHT', 'BOTH'] else LcdSide.BOTH
        except Exception:
            lcd_side = LcdSide.BOTH

        def bg_task():
            try:
                # 延时启动
                if delay_ms and delay_ms > 0:
                    logger.info(f"[ZMQ] play_sequence_animations: 延时 {delay_ms} ms 后启动")
                    time.sleep(delay_ms / 1000.0)
                # 选择调用精灵动画或序列动画
                if sprite:
                    overlay_id = self.engine.play_overlay_sprite(
                        sprite, side=lcd_side, loop=loop, fps=fps, speed=speed,
                        x=x, y=y, scale=scale, rotation=rotation, duration_ms=duration_ms
                    )
                else:
                    overlay_id = self.engine.play_sequence_animations(
                        sequence, side=lcd_side, loop=loop, loop_count=loop_count, fps=fps, speed=speed, clear_time=clear_time, exclusive=exclusive
                    )
                if not overlay_id:
                    logger.error(f"play_sequence_animations: 启动失败: sequence={sequence}, sprite={sprite}")
                    return
                # 发布事件
                try:
                    self._publish_event('overlay.started', {
                        "overlay_id": overlay_id,
                        "sequence": sequence,
                        "sprite": sprite,
                        "side": lcd_side.name,
                        "loop": loop,
                        "x": x, "y": y, "scale": scale, "rotation": rotation, "duration_ms": duration_ms
                    })
                except Exception:
                    logger.exception("play_sequence_animations: unable to publish overlay.started event")
                logger.info(f"[ZMQ] play_sequence_animations: overlay_id={overlay_id} 启动成功")
            except Exception:
                logger.exception("play_sequence_animations: exception while starting overlay")

        t = threading.Thread(target=bg_task, daemon=True)
        t.start()

        return {"success": True, "task_id": f"play_overlay_{sprite or sequence}"}

    def _cmd_play_text_overlay(self, cmd: Dict, priority: int) -> Dict:
        """在屏幕上播放文本 overlay（把文本渲染为临时 seq 或直接由 renderer 处理）"""
        text = cmd.get('text')
        font = cmd.get('font', 'DEFAULT')
        color = cmd.get('color', '#FFFFFF')
        bg_color = cmd.get('bg_color', '#00000000')
        side = cmd.get('side', 'BOTH')
        align = cmd.get('align', 'center')
        scroll = cmd.get('scroll', 'none')
        speed = float(cmd.get('speed', 1.0))
        loop = bool(cmd.get('loop', False))
        loop_count = cmd.get('loop_count')
        duration_ms = cmd.get('duration_ms')

        if not text:
            return {"success": False, "error": "缺少 text 字段"}

        try:
            lcd_side = LcdSide[side.upper()] if side and side.upper() in ['LEFT', 'RIGHT', 'BOTH'] else LcdSide.BOTH
        except Exception:
            lcd_side = LcdSide.BOTH

        def task():
            # 如果 EyeEngine 未提供 play_text_overlay，则在这里将文本渲染为临时 PNG，再复用 play_overlay_image
            try:
                from PIL import Image, ImageDraw, ImageFont
                import tempfile
                # 简单字体处理：尝试使用 truetype，否则回退到默认字体
                try:
                    pil_font = ImageFont.truetype(str(font), 20) if font and isinstance(font, str) and font.lower() != 'default' else ImageFont.load_default()
                except Exception:
                    pil_font = ImageFont.load_default()

                # 估算文本尺寸
                dummy_img = Image.new('RGBA', (1, 1), (0, 0, 0, 0))
                draw = ImageDraw.Draw(dummy_img)
                text_w, text_h = draw.textsize(text, font=pil_font)
                pad = 8
                img_w = max(1, text_w + pad * 2)
                img_h = max(1, text_h + pad * 2)

                img = Image.new('RGBA', (img_w, img_h), tuple(int(bg_color.lstrip('#')[i:i+2], 16) for i in (0,2,4)) + ((0,) if len(bg_color.lstrip('#'))==6 else (int(bg_color.lstrip('#')[6:8],16),)))
                draw = ImageDraw.Draw(img)
                # 颜色解析
                try:
                    rgb = tuple(int(color.lstrip('#')[i:i+2], 16) for i in (0,2,4))
                except Exception:
                    rgb = (255,255,255)
                draw.text((pad, pad), text, font=pil_font, fill=rgb)

                tmp = tempfile.NamedTemporaryFile(delete=False, suffix='.png')
                tmp_path = tmp.name
                tmp.close()
                img.save(tmp_path)

                # 使用 engine 提供的 play_overlay_image 将 PNG 转为 seq 并播放
                try:
                    overlay_id = self.engine.play_overlay_image(tmp_path, side=lcd_side, loop=loop, fps=None, speed=speed)
                finally:
                    # 清理临时 PNG
                    try:
                        os.unlink(tmp_path)
                    except Exception:
                        pass

                if not overlay_id:
                    raise RuntimeError('无法启动文本 overlay')
                return overlay_id
            except Exception:
                logger.exception("play_text_overlay task failed")
                raise

        task_id = f"play_text_overlay_{hash(text) & 0xfffff}"

        def bg_task_wrapper():
            try:
                overlay_id = task()
                try:
                    self._publish_event('overlay.started', {"overlay_id": overlay_id, "text": text, "side": lcd_side.name})
                except Exception:
                    logger.exception("_cmd_play_text_overlay: unable to publish overlay.started event")
            except Exception:
                logger.exception("_cmd_play_text_overlay: background task failed")

        accepted = self.task_manager.submit_task(task_id, lambda: threading.Thread(target=bg_task_wrapper, daemon=True).start(), priority, blocking=False)
        if not accepted:
            return self._priority_reject()

        return {"success": True, "task_id": task_id}

    def _cmd_play_overlay_sequence_sync(self, cmd: Dict, priority: int) -> Dict:
        """同步执行 overlay 播放并返回 overlay_id（尊重优先级检查）"""
        sequence = cmd.get('sequence')
        fps = cmd.get('fps')
        loop = cmd.get('loop', False)
        loop_count = cmd.get('loop_count')
        delay_ms = cmd.get('delay_ms', 0)
        side = cmd.get('side', 'BOTH')
        speed = float(cmd.get('speed', 1.0))
        clear_time = int(cmd.get('clear_time', 0))
        exclusive = bool(cmd.get('exclusive', False))

        if not sequence:
            return {"success": False, "error": "缺少 sequence 名称"}

        try:
            lcd_side = LcdSide[side.upper()] if side.upper() in ['LEFT', 'RIGHT', 'BOTH'] else LcdSide.BOTH
        except Exception:
            lcd_side = LcdSide.BOTH

        def play_cb():
            overlay_id = self.engine.play_sequence_animations(sequence, side=lcd_side, loop=loop, loop_count=loop_count, fps=fps, speed=speed, clear_time=clear_time, exclusive=exclusive)
            if not overlay_id:
                raise RuntimeError("无法启动 overlay 序列")
            return overlay_id

        accepted, result = self.task_manager.submit_task_sync(f"play_overlay_sync_{sequence}", play_cb, priority=priority)
        if not accepted:
            return self._priority_reject()
        # result is overlay_id or traceback string
        if isinstance(result, str) and result.startswith('Traceback'):
            return {"success": False, "error": result}
        # 发布事件
        try:
            self._publish_event('overlay.started', {"overlay_id": result, "sequence": sequence, "side": lcd_side.name, "loop": loop})
        except Exception:
            logger.exception("_cmd_play_overlay_sequence_sync: unable to publish overlay.started event")
        return {"success": True, "overlay_id": result}

    def _cmd_stop_overlay_sequence_sync(self, cmd: Dict, priority: int) -> Dict:
        """同步停止指定 overlay 并立即返回结果（尊重优先级检查）"""
        overlay_id = cmd.get('overlay_id')
        if not overlay_id:
            return {"success": False, "error": "缺少 overlay_id"}

        def stop_cb():
            ok = self.engine.stop_overlay_sequence(overlay_id)
            if not ok:
                raise RuntimeError(f"stop_overlay_sequence failed for {overlay_id}")
            return True

        accepted, result = self.task_manager.submit_task_sync(f"stop_overlay_sync_{overlay_id}", stop_cb, priority=priority)
        if not accepted:
            return self._priority_reject()
        if isinstance(result, str) and result.startswith('Traceback'):
            return {"success": False, "error": result}
        try:
            self._publish_event('overlay.stopped', {"overlay_id": overlay_id})
        except Exception:
            logger.exception("_cmd_stop_overlay_sequence_sync: unable to publish overlay.stopped event")
        return {"success": True, "stopped": True}

    def _cmd_stop_overlay_sequence(self, cmd: Dict, priority: int) -> Dict:
        """停止指定 overlay（传入 overlay_id）"""
        overlay_id = cmd.get('overlay_id')
        if not overlay_id:
            return {"success": False, "error": "缺少 overlay_id"}

        def task():
            ok = self.engine.stop_overlay_sequence(overlay_id)
            if ok:
                self._publish_event('overlay.stopped', {"overlay_id": overlay_id})
            else:
                raise RuntimeError(f"stop_overlay_sequence failed for {overlay_id}")

        task_id = f"stop_overlay_{overlay_id}"
        accepted = self.task_manager.submit_task(task_id, task, priority, blocking=False)
        if not accepted:
            return self._priority_reject()
        return {"success": True, "task_id": task_id}

    def _cmd_play_all_sequences(self, cmd: Dict, priority: int) -> Dict:
        """播放所有序列"""
        fps = cmd.get('fps')
        loop = cmd.get('loop', False)
        
        def task():
            sequences = self.engine.asset_manager.get_available_sequences()
            if not sequences:
                logger.warning("No sequences available to play")
                return
                
            for seq in sequences:
                if self.task_manager.should_stop():
                    break
                logger.info(f"Playing sequential sequence: {seq}")
                player = self.engine.play_sequence(seq, fps=fps, loop=False) # 内部循环不使用 loop，由外部逻辑控制
                if player:
                    while player.is_playing and self.running:
                        time.sleep(0.05)
                        if self.task_manager.should_stop():
                            player.stop()
                            break
                
            if loop and self.running and not self.task_manager.should_stop():
                # 如果需要循环播放全部，则重新提交自身
                self.task_manager.submit_task(f"play_all_seq_loop", task, priority, blocking=False)

        task_id = "play_all_sequences"
        accepted = self.task_manager.submit_task(task_id, task, priority, blocking=False)
        if not accepted:
            return self._priority_reject()
        return {"success": True, "task_id": task_id}

    def _cmd_blink(self, cmd: Dict, priority: int) -> Dict:
        """眨眼"""
        # 如果控制器正忙（正在播放动画），直接告知客户端而不是接受任务并被忽略
        try:
            if getattr(self.engine.controller, '_animating', False):
                return {"success": False, "error": "controller_busy", "reason": "animating"}
        except Exception:
            pass

        def task():
            return self.engine.controller.blink()

        task_id = "blink"
        accepted = self.task_manager.submit_task(task_id, task, priority, blocking=False)
        if not accepted:
            return self._priority_reject()
        return {"success": True, "task_id": task_id}

    def _cmd_set_gaze(self, cmd: Dict, priority: int) -> Dict:
        """
        设置眼睛注视方向（眼神跟随）
        
        Args:
            cmd: 包含 x, y 坐标的命令字典
                x: 水平方向 (-1.0=左, 1.0=右, 0=中间)
                y: 垂直方向 (-1.0=上, 1.0=下, 0=中间)
            priority: 任务优先级
        """
        x = cmd.get('x', 0.0)
        y = cmd.get('y', 0.0)
        
        # 验证参数范围
        try:
            x = float(x)
            y = float(y)
            x = max(-1.0, min(1.0, x))
            y = max(-1.0, min(1.0, y))
        except (TypeError, ValueError):
            return {"success": False, "error": "invalid_coordinates", 
                    "message": "x 和 y 必须是 -1.0 到 1.0 之间的数值"}
        
        def task():
            self.engine.controller.look_at(x, y)
            return True
        
        task_id = f"set_gaze_{x:.2f}_{y:.2f}"
        accepted = self.task_manager.submit_task(task_id, task, priority, blocking=False)
        if not accepted:
            return self._priority_reject()
        
        logger.debug(f"[set_gaze] 眼神移动到: ({x:.2f}, {y:.2f})")
        return {"success": True, "task_id": task_id, "x": x, "y": y}

    def _cmd_debug_fail(self, cmd: Dict, priority: int) -> Dict:
        """调试: 提交一个总会抛出异常的任务（仅在 config.zmq_service.debug_commands 启用时可用）"""
        if not self.cfg['zmq_service'].get('debug_commands'):
            return {"success": False, "error": "调试命令被禁用"}

        def task():
            raise RuntimeError("debug_fail invoked")

        task_id = "debug_fail"
        accepted = self.task_manager.submit_task(task_id, task, priority, blocking=False)
        if not accepted:
            return self._priority_reject()
        return {"success": True, "task_id": task_id}

    def _cmd_stop(self) -> Dict:
        """停止当前任务"""
        stopped = self.task_manager.stop_current_task()
        return {"success": True, "stopped": stopped}
    
    # ===== Eye Visibility Management Commands =====
    
    def _cmd_show_widget(self, cmd: Dict) -> Dict:
        """显示 widget（隐藏 eye）"""
        try:
            widget_name = cmd.get('widget')
            if not widget_name:
                return {"success": False, "error": "缺少 widget 参数"}
            
            timeout = cmd.get('timeout')  # None 或整数
            self.visibility_manager.show_widget(widget_name, timeout)
            
            # 暂停 eyeEngine 渲染
            self.engine.pause_lcd()
            logger.info(f"eyeEngine LCD 已暂停，widget '{widget_name}' 将控制 LCD")
            
            return {
                "success": True,
                "widget": widget_name,
                "timeout": timeout,
                "status": self.visibility_manager.get_status()
            }
        except Exception as e:
            logger.error(f"show_widget 失败: {e}", exc_info=True)
            return {"success": False, "error": str(e)}
    
    def _cmd_restore_eye(self, cmd: Dict) -> Dict:
        """恢复 eye 显示（隐藏 widget）"""
        try:
            reason = cmd.get('reason', 'manual')
            self.visibility_manager.restore_eye(reason)
            
            # 恢复 eyeEngine 渲染
            self.engine.resume_lcd()
            logger.info(f"eyeEngine LCD 已恢复，原因: {reason}")
            
            return {
                "success": True,
                "reason": reason,
                "status": self.visibility_manager.get_status()
            }
        except Exception as e:
            logger.error(f"restore_eye 失败: {e}", exc_info=True)
            return {"success": False, "error": str(e)}
    
    def _cmd_pause_auto_restore(self, cmd: Dict) -> Dict:
        """暂停自动恢复"""
        try:
            self.visibility_manager.pause_auto_restore()
            return {
                "success": True,
                "status": self.visibility_manager.get_status()
            }
        except Exception as e:
            logger.error(f"pause_auto_restore 失败: {e}", exc_info=True)
            return {"success": False, "error": str(e)}
    
    def _cmd_resume_auto_restore(self, cmd: Dict) -> Dict:
        """恢复自动恢复"""
        try:
            timeout = cmd.get('timeout')  # None 或整数
            self.visibility_manager.resume_auto_restore(timeout)
            return {
                "success": True,
                "timeout": timeout,
                "status": self.visibility_manager.get_status()
            }
        except Exception as e:
            logger.error(f"resume_auto_restore 失败: {e}", exc_info=True)
            return {"success": False, "error": str(e)}
    
    def _cmd_set_manual_mode(self, cmd: Dict) -> Dict:
        """设置手动控制模式"""
        try:
            enabled = cmd.get('enabled', True)
            self.visibility_manager.set_manual_mode(enabled)
            return {
                "success": True,
                "manual_mode": enabled,
                "status": self.visibility_manager.get_status()
            }
        except Exception as e:
            logger.error(f"set_manual_mode 失败: {e}", exc_info=True)
            return {"success": False, "error": str(e)}
    
    def _cmd_get_visibility_status(self, cmd: Dict) -> Dict:
        """获取显示状态"""
        try:
            return {
                "success": True,
                "status": self.visibility_manager.get_status()
            }
        except Exception as e:
            logger.error(f"get_visibility_status 失败: {e}", exc_info=True)
            return {"success": False, "error": str(e)}

    # ===== Video Stream Commands =====
    def _video_stream_status_payload(self) -> Dict:
        status = self.engine.get_video_stream_status()
        status["active"] = bool(getattr(self.engine, "_video_stream_active", False))
        status["manual_override"] = bool(self._video_stream_manual_override)
        status["target_lcd"] = getattr(self.engine._config, "video_stream_target_lcd", "RIGHT")
        return status

    def _cmd_enable_video_stream(self, cmd: Dict) -> Dict:
        """启用视频流显示"""
        try:
            target_lcd = cmd.get("target_lcd")
            fps = cmd.get("fps")
            display_mode = cmd.get("display_mode")
            overlay_style = cmd.get("overlay_style")
            if target_lcd is not None:
                if isinstance(target_lcd, str):
                    target_text = target_lcd.strip()
                    self.engine._config.video_stream_target_lcd = int(target_text) if target_text.isdigit() else target_text.upper()
                else:
                    self.engine._config.video_stream_target_lcd = int(target_lcd)
            if fps is not None:
                self.engine._config.video_stream_fps = int(fps)
            if display_mode is not None:
                self.engine._config.video_stream_display_mode = str(display_mode)
            if overlay_style is not None:
                self.engine._config.video_stream_overlay_style = str(overlay_style)
            self._video_stream_manual_override = True
            ok = self.engine.start_video_stream()
            if ok and hasattr(self.engine, "set_video_stream_visibility"):
                self.engine.set_video_stream_visibility(True, refresh=False)
            elif ok and hasattr(self.engine, "set_video_stream_active"):
                self.engine.set_video_stream_active(True)
            return {"success": ok, "status": self._video_stream_status_payload()}
        except Exception as e:
            logger.error(f"enable_video_stream 失败: {e}", exc_info=True)
            return {"success": False, "error": str(e)}

    def _cmd_disable_video_stream(self, cmd: Dict) -> Dict:
        """禁用视频流显示"""
        try:
            self._video_stream_manual_override = False
            if hasattr(self.engine, "set_video_stream_visibility"):
                self.engine.set_video_stream_visibility(False, clear_cached_frame=True, refresh=True)
            elif hasattr(self.engine, "set_video_stream_active"):
                self.engine.set_video_stream_active(False)
            self.engine.stop_video_stream()
            return {"success": True, "status": self._video_stream_status_payload()}
        except Exception as e:
            logger.error(f"disable_video_stream 失败: {e}", exc_info=True)
            return {"success": False, "error": str(e)}

    def _cmd_video_stream_status(self, cmd: Dict) -> Dict:
        """查询视频流状态"""
        try:
            return {"success": True, "status": self._video_stream_status_payload()}
        except Exception as e:
            logger.error(f"video_stream_status 失败: {e}", exc_info=True)
            return {"success": False, "error": str(e)}
    
    # ===== End Visibility Management Commands =====
    
    def _cmd_get_status(self) -> Dict:
        """获取状态"""
        # Try to include FPS stats if available
        player = getattr(self.engine, 'eye_animation_player', None)
        current_fps = None
        last_avg_fps = None
        if player:
            try:
                current_fps = player.current_fps
                last_avg_fps = player.last_average_fps
            except Exception:
                current_fps = None
                last_avg_fps = None

        is_animating = False
        try:
            is_animating = bool(self.engine.controller._animating)
        except Exception:
            is_animating = False
        priority_status = None
        try:
            priority_status = self.task_manager.get_snapshot()
        except Exception:
            logger.exception("获取优先级快照失败")
        return {
            "success": True,
            "status": {
                "current_task": self.task_manager.get_current_task_id(),
                "current_priority": self.task_manager.get_current_priority(),
                "is_running": self.task_manager.is_running(),
                "is_animating": is_animating,
                "current_fps": current_fps,
                "last_average_fps": last_avg_fps,
                "priority": priority_status
            }
        }
    
    def _cmd_list_categories(self) -> Dict:
        """列出分类"""

    def _cmd_list_sequences(self) -> Dict:
        """列出序列文件"""
        sequences = self.engine.asset_manager.get_available_sequences()
        return {"success": True, "sequences": sequences}
    
    def _cmd_list_behaviors(self) -> Dict:
        """列出行为"""
        if self.engine.behavior_manager:
            behaviors = self.engine.behavior_manager.get_available_behaviors()
            return {"success": True, "behaviors": behaviors}
        return {"success": False, "error": "BehaviorManager 未初始化"}
    
    def _cmd_list_iris(self) -> Dict:
        """列出虹膜类型和样式"""
        types = self.engine.asset_manager.get_available_iris_types()
        iris_data = {}
        for t in types:
            styles = self.engine.asset_manager.get_available_iris_styles(t)
            iris_data[t] = styles
        return {"success": True, "iris": iris_data}
    
    def _cmd_list_backgrounds(self) -> Dict:
        try:
            backgrounds = {}
            types = self.engine.asset_manager.get_available_background_types()
            for t in types:
                backgrounds[t] = self.engine.asset_manager.get_available_background_styles(t)
            return {"success": True, "backgrounds": backgrounds}
        except Exception as e:
            return {"success": False, "error": str(e)}

    def run(self):
        """主运行循环，接收并处理 ZMQ 命令"""
        self.running = True
        logger.info("服务开始运行")
        try:
            while self.running:
                try:
                    # 接收命令（超时 1 秒）
                    if self.cmd_socket.poll(1000):
                        msg = self.cmd_socket.recv_json()
                        action = msg.get('action', 'unknown')
                        logger.info(f"[RUN] 收到命令: action={action}")

                        # 处理命令，加 try-except 捕获异常
                        try:
                            response = self.handle_command(msg)
                            logger.info(f"[RUN] handle_command 完成: action={action}, success={response.get('success')}")
                        except Exception as e:
                            logger.error(f"[RUN] handle_command 异常: action={action}: {e}", exc_info=True)
                            response = {"success": False, "error": str(e)}

                        # 发送响应
                        logger.info(f"[RUN] 发送响应: action={action}")
                        self.cmd_socket.send_json(response)
                        logger.info(f"[RUN] 响应已发送: action={action}")

                except zmq.ZMQError as e:
                    if e.errno == zmq.ETERM:
                        break
                    logger.error(f"[RUN] ZMQ 错误: {e}")
                except Exception as e:
                    logger.error(f"[RUN] 处理命令异常: {e}", exc_info=True)
                    try:
                        self.cmd_socket.send_json({"success": False, "error": str(e)})
                    except Exception:
                        pass

        finally:
            self.shutdown()
    
    def shutdown(self):
        """关闭服务，确保所有线程安全退出"""
        logger.info("正在关闭服务...（shutdown called）")
        self.running = False

        # 停止 widget 监听器
        self._widget_listener_running = False
        if self._widget_listener_thread and self._widget_listener_thread.is_alive():
            self._widget_listener_thread.join(timeout=1.0)
            logger.info("Widget 监听线程已停止")
        
        # 停止 vision 监听器
        self._vision_listener_running = False
        if self._vision_listener_thread and self._vision_listener_thread.is_alive():
            self._vision_listener_thread.join(timeout=1.0)
            logger.info("Vision 监听线程已停止")

        # 停止表情轮播
        if self.carousel_thread:
            self.carousel_stop_event.set()
            self.carousel_thread.join(timeout=1.0)

        # 停止当前任务
        self.task_manager.stop_current_task()

        # 主动遍历所有非主线程并join，增强健壮性
        import threading
        main_thread = threading.current_thread()
        alive_threads = [t for t in threading.enumerate() if t is not main_thread and t.is_alive()]
        if alive_threads:
            logger.info(f"shutdown: 等待存活线程退出: {[t.name for t in alive_threads]}")
        for t in alive_threads:
            try:
                t.join(timeout=1.0)
            except Exception as e:
                logger.warning(f"shutdown: join线程失败: {t.name}: {e}")

        # 关闭 ZMQ
        try:
            if self._widget_sub_socket:
                self._widget_sub_socket.close()
        except Exception:
            pass
        try:
            self.cmd_socket.close()
        except Exception:
            pass
        try:
            self.event_socket.close()
        except Exception:
            pass
        try:
            self.zmq_ctx.term()
        except Exception:
            pass

        # 释放引擎
        try:
            self.engine.release()
        except Exception:
            pass

        logger.info("服务已关闭，所有线程已处理")
        # 兜底强制退出，防止主线程被阻塞卡死
        import os
        os._exit(0)



def main():
    parser = argparse.ArgumentParser(description='EyeEngine ZMQ 服务')
    parser.add_argument('--config', type=str, help='用户配置文件路径')
    parser.add_argument('--mock', action='store_true', help='使用模拟驱动')
    parser.add_argument('--log-level', type=str, default='INFO', 
                       choices=['DEBUG', 'INFO', 'WARNING', 'ERROR'],
                       help='日志级别')
    
    args = parser.parse_args()
    
    # 设置日志级别
    log_level = getattr(logging, args.log_level)
    logging.basicConfig(
        level=log_level,
        format='[%(asctime)s] [%(levelname)s] %(name)s: %(message)s'
    )
    
    # 创建服务
    service = EyeEngineZmqService(config_file=args.config, use_mock=args.mock)

    
    # 信号处理
    def signal_handler(sig, frame):
        logger.info("收到中断信号，正在退出...")
        service.shutdown()
        sys.exit(0)
    
    signal.signal(signal.SIGINT, signal_handler)
    signal.signal(signal.SIGTERM, signal_handler)
    
    # 运行服务
    service.run()


if __name__ == '__main__':
    main()
