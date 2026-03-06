"""
ZMQ 眼睛动画接口实现

通过 ZMQ REQ-REP 模式与 EyeEngine 通信，支持：
- 播放眼睛动画（按分类、名称、ID、行为）
- 停止当前动画
- 设置虹膜、背景等眼睛属性

日志格式：[ZMQEyeInterface] 操作描述

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import asyncio
import logging
import json
import time
from typing import Optional, Dict, Any
from pathlib import Path
import sys

import zmq

# 导入抽象基类 - 从父目录的 hardware_interfaces.py
from ..hardware_interfaces import EyeInterface


logger = logging.getLogger(__name__)


class ZMQEyeInterface(EyeInterface):

    async def play_sprite_animation(
        self,
        category: str,
        animation: str,
        start: int = 0,
        loop: bool = False,
        loop_count: int = 0,
        speed: float = 1.0,
        clear_time: int = 0,
        side: str = "BOTH"
    ) -> Optional[str]:
        """
        播放 config/eye/eyeanimations.xml 中定义的 SpriteAnimation
        参数:
            category: 动画分类
            animation: 动画名称
            start: 起始帧
            loop: 是否循环
            loop_count: 循环次数（0为无限）
            speed: 播放速度（倍速）
            clear_time: 播放结束后清屏时间(ms)
            side: 显示屏幕（LEFT/RIGHT/BOTH）
        """
        cmd = {
            'action': 'play_sprite_animation',
            'category': category,
            'animation': animation,
            'start': start,
            'loop': loop,
            'loop_count': loop_count,
            'speed': speed,
            'clear_time': clear_time,
            'side': side
        }
        try:
            response = self._send_command(cmd)
            if not response.get('success', False):
                raise RuntimeError(f"Failed to play sprite animation: {response.get('error')}")
            return response.get('overlay_id') or response.get('task_id')
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] play_sprite_animation failed: {e}")
            raise
    """通过 ZMQ 控制眼睛动画的接口实现"""
    
    # ZMQ 端点默认值（与 EyeEngine zmq_service.py 一致）
    DEFAULT_COMMAND_ENDPOINT = "ipc:///tmp/doly_eye_cmd.sock"
    DEFAULT_EVENT_ENDPOINT = "ipc:///tmp/doly_eye_event.sock"
    DEFAULT_TIMEOUT_MS = 5000
    
    def __init__(
        self,
        cmd_endpoint: str = DEFAULT_COMMAND_ENDPOINT,
        event_endpoint: str = DEFAULT_EVENT_ENDPOINT,
        timeout_ms: int = DEFAULT_TIMEOUT_MS,
        debug: bool = False
    ):
        """
        初始化 ZMQ 眼睛动画接口
        
        Args:
            cmd_endpoint: ZMQ 命令端点（REQ-REP）
            event_endpoint: ZMQ 事件端点（PUB-SUB）
            timeout_ms: 命令超时时间（毫秒）
            debug: 是否启用调试日志
            
        Raises:
            RuntimeError: 如果连接失败
        """
        self.cmd_endpoint = cmd_endpoint
        self.event_endpoint = event_endpoint
        self.timeout_ms = timeout_ms
        self.debug = debug
        
        self._ctx: Optional[zmq.Context] = None
        self._cmd_socket: Optional[zmq.Socket] = None
        self._event_socket: Optional[zmq.Socket] = None
        self._connected = False
        
        # 本地缓存：是否启用优先级（默认启用）。当禁用时，接口不会在命令中传递 priority 字段
        self.priority_enabled = True
        
        logger.info(f"[ZMQEyeInterface] 初始化，命令端点: {cmd_endpoint}, "
                   f"事件端点: {event_endpoint}, 超时: {timeout_ms}ms")
        
        self._connect()
    
    def _connect(self) -> None:
        """建立 ZMQ 连接"""
        try:
            # 创建 ZMQ 上下文
            self._ctx = zmq.Context()
            
            # 创建命令 socket (REQ-REP)
            self._cmd_socket = self._ctx.socket(zmq.REQ)
            self._cmd_socket.setsockopt(zmq.LINGER, 0)
            self._cmd_socket.setsockopt(zmq.RCVTIMEO, self.timeout_ms)
            self._cmd_socket.connect(self.cmd_endpoint)
            
            # 创建事件 socket (PUB-SUB，可选，用于监听动画完成事件)
            self._event_socket = self._ctx.socket(zmq.SUB)
            self._event_socket.setsockopt(zmq.LINGER, 0)
            self._event_socket.setsockopt(zmq.RCVTIMEO, 100)  # 非阻塞监听
            self._event_socket.setsockopt_string(zmq.SUBSCRIBE, "")
            try:
                self._event_socket.connect(self.event_endpoint)
                logger.debug(f"[ZMQEyeInterface] 事件 socket 连接成功")
            except Exception as e:
                logger.warning(f"[ZMQEyeInterface] 事件 socket 连接失败: {e}, 将使用延迟等待")
                self._event_socket = None
            
            self._connected = True
            logger.info(f"[ZMQEyeInterface] ZMQ 连接成功")
            
            # lock to ensure REQ socket send/recv are not used concurrently
            import threading
            self._cmd_lock = threading.Lock()
            
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] ZMQ 连接失败: {e}")
            self._cleanup()
            raise RuntimeError(f"Failed to connect to EyeEngine via ZMQ: {e}")
    
    def _cleanup(self) -> None:
        """清理资源"""
        if self._cmd_socket:
            try:
                self._cmd_socket.close()
            except:
                pass
            self._cmd_socket = None
        
        if self._event_socket:
            try:
                self._event_socket.close()
            except:
                pass
            self._event_socket = None
        
        if self._ctx:
            try:
                self._ctx.term()
            except:
                pass
            self._ctx = None
        
        self._connected = False
    
    def _send_command(self, cmd: Dict[str, Any]) -> Dict[str, Any]:
        """
        发送命令到 EyeEngine
        
        Args:
            cmd: 命令字典
            
        Returns:
            响应字典
            
        Raises:
            RuntimeError: 如果连接丢失或超时
        """
        if not self._connected or self._cmd_socket is None:
            raise RuntimeError("ZMQ connection not established")
        
        with self._cmd_lock:
            try:
                # 序列化和发送命令
                cmd_json = json.dumps(cmd)
                if self.debug:
                    logger.debug(f"[ZMQEyeInterface] 发送命令: {cmd_json}")

                # Ensure only one thread performs send/recv on REQ socket at a time
                # (previous code mistakenly nested the same lock twice which caused deadlock)
                self._cmd_socket.send_json(cmd)
                response = self._cmd_socket.recv_json()

                if self.debug:
                    logger.debug(f"[ZMQEyeInterface] 收到响应: {response}")

                return response

            except zmq.error.Again:
                # On timeout, the REQ socket may be left waiting for a reply and
                # further send/recv calls will fail with 'Operation cannot be
                # accomplished in current state'. Recover by cleaning up and
                # reconnecting so future commands can proceed.
                try:
                    logger.warning(f"[ZMQEyeInterface] command timeout, attempting reconnect")
                    self._cleanup()
                    self._connect()
                except Exception:
                    logger.exception("[ZMQEyeInterface] reconnect after timeout failed")
                raise RuntimeError(f"ZMQ command timeout ({self.timeout_ms}ms)")
            except json.JSONDecodeError as e:
                raise RuntimeError(f"Invalid JSON response from EyeEngine: {e}")
            except Exception as e:
                # On generic zmq errors, attempt to recover the connection so
                # the interface remains usable for subsequent commands.
                try:
                    logger.warning(f"[ZMQEyeInterface] communication error: {e}, attempting reconnect")
                    self._cleanup()
                    self._connect()
                except Exception:
                    logger.exception("[ZMQEyeInterface] reconnect after communication error failed")
                raise RuntimeError(f"ZMQ communication error: {e}")
    
    def _wait_for_event(
        self,
        task_id: Optional[str] = None,
        timeout_s: float = 10.0
    ) -> bool:
        """
        等待任务完成事件
        
        Args:
            task_id: 任务 ID（如果为 None，则不匹配特定任务）
            timeout_s: 超时时间（秒）
            
        Returns:
            True 如果收到完成事件，False 如果超时
        """
        if self._event_socket is None:
            # 如果事件 socket 不可用，使用简单延迟
            if self.debug:
                logger.debug(f"[ZMQEyeInterface] 使用延迟等待而非事件监听")
            asyncio.run(asyncio.sleep(0.5))
            return True
        
        start_time = time.time()
        while time.time() - start_time < timeout_s:
            try:
                event_json = self._event_socket.recv_string()
                
                # 跳过空消息
                if not event_json or not event_json.strip():
                    continue
                
                event = json.loads(event_json)
                
                if event.get("type") == "task.complete":
                    event_task_id = event.get("task_id")
                    if task_id is None or event_task_id == task_id:
                        logger.info(f"[ZMQEyeInterface] 动画完成事件: task_id={event_task_id}")
                        return True
                
            except zmq.error.Again:
                # 超时，继续等待
                continue
            except Exception as e:
                logger.debug(f"[ZMQEyeInterface] 事件监听错误: {e}")
        
        logger.warning(f"[ZMQEyeInterface] 等待动画完成事件超时 ({timeout_s}s)")
        return False
    
    async def play_animation(
        self,
        category: str,
        animation: str,
        priority: int = 5,
        wait_completion: bool = False,
        hold_duration: float = 0.0
    ) -> None:
        """
        播放眼睛动画
        
        Args:
            category: 动画分类
            animation: 动画名称
            priority: 任务优先级（1-10，默认 5）
            wait_completion: 是否等待动画完成
            hold_duration: 动画播放完后保持状态的时长（秒）
        """
        cmd = {
            "action": "play_animation",
            "animation": animation,
            "category": category,
            "hold_duration": hold_duration
        }
        if self.priority_enabled:
            cmd["priority"] = priority

        if self.priority_enabled:
            logger.info(f"[ZMQEyeInterface] 播放动画: category={category}, animation={animation}, "
                       f"priority={priority}, hold_duration={hold_duration}")
        else:
            logger.info(f"[ZMQEyeInterface] 播放动画: category={category}, animation={animation}, "
                       f"hold_duration={hold_duration} (priority disabled)")

        try:
            response = self._send_command(cmd)
            
            if not response.get("success", False):
                error_msg = response.get("error", "Unknown error")
                raise RuntimeError(f"Failed to play animation: {error_msg}")
            
            task_id = response.get("task_id")
            logger.info(f"[ZMQEyeInterface] 命令已发送，task_id={task_id}")
            
            # 如果需要等待完成
            if wait_completion and task_id:
                # 设置一个足够长的超时时间，考虑 hold_duration
                # 默认 30 秒通常足够绝大多数动画 + hold
                wait_timeout = max(30.0, hold_duration + 20.0)
                await asyncio.to_thread(self._wait_for_event, task_id, wait_timeout)
            
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] 播放动画失败: {e}")
            raise

    async def play_behavior(
        self,
        behavior: str,
        level: int = 1,
        priority: int = 5,
        wait_completion: bool = False,
        hold_duration: float = 0.0
    ) -> None:
        """
        播放行为动画
        
        Args:
            behavior: 行为名称
            level: 分类等级
            priority: 任务优先级（1-10，默认 5）
            wait_completion: 是否等待动画完成
            hold_duration: 动画播放完后保持状态的时长（秒）
        """
        cmd = {
            "action": "play_behavior",
            "behavior": behavior,
            "level": level,
            "hold_duration": hold_duration
        }
        if self.priority_enabled:
            cmd["priority"] = priority

        if self.priority_enabled:
            logger.info(f"[ZMQEyeInterface] 播放行为: behavior={behavior}, level={level}, "
                       f"priority={priority}, hold_duration={hold_duration}")
        else:
            logger.info(f"[ZMQEyeInterface] 播放行为: behavior={behavior}, level={level}, "
                       f"hold_duration={hold_duration} (priority disabled)")

        try:
            response = self._send_command(cmd)

            if not response.get("success", False):
                error_msg = response.get("error", "Unknown error")
                raise RuntimeError(f"Failed to play behavior: {error_msg}")

            task_id = response.get("task_id")
            logger.info(f"[ZMQEyeInterface] 行为指令已发送，task_id={task_id}")

            # 如果需要等待完成
            if wait_completion and task_id:
                # 考虑 hold_duration 对等待超时的影响
                wait_timeout = max(30.0, hold_duration + 20.0)
                await asyncio.to_thread(self._wait_for_event, task_id, wait_timeout)

        except Exception as e:
            logger.error(f"[ZMQEyeInterface] 播放行为失败: {e}")
            raise
    
    async def set_iris(self, theme: str, style: str, side: str = "BOTH") -> None:
        """设置虹膜"""
        cmd = {
            "action": "set_iris",
            "theme": theme,
            "style": style,
            "side": side
        }
        logger.info(f"[ZMQEyeInterface] set_iris: theme={theme}, style={style}, side={side}")
        try:
            self._send_command(cmd)
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] set_iris failed: {e}")
            raise

    async def set_lid(self, side_id: str = None, top_id: str = None, 
                      bottom_id: str = None, side: str = "BOTH") -> None:
        """
        设置眼睑样式
        
        Args:
            side_id: 眼睑侧边ID（可选）
            top_id: 上眼睑ID（可选）
            bottom_id: 下眼睑ID（可选）
            side: 显示屏幕（LEFT/RIGHT/BOTH）
        """
        cmd = {
            "action": "set_lid",
            "side": side
        }
        if side_id is not None:
            cmd["side_id"] = side_id
        if top_id is not None:
            cmd["top_id"] = top_id
        if bottom_id is not None:
            cmd["bottom_id"] = bottom_id
        
        logger.info(f"[ZMQEyeInterface] set_lid: side_id={side_id}, top_id={top_id}, bottom_id={bottom_id}, side={side}")
        try:
            self._send_command(cmd)
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] set_lid failed: {e}")
            raise

    async def set_brightness(self, brightness: int, side: str = "BOTH") -> None:
        """
        设置屏幕亮度
        
        Args:
            brightness: 亮度值 (0-255)
            side: 显示屏幕（LEFT/RIGHT/BOTH）
        """
        if not 0 <= brightness <= 255:
            raise ValueError(f"Brightness must be between 0 and 255, got {brightness}")
        
        cmd = {
            "action": "set_brightness",
            "brightness": brightness,
            "side": side
        }
        logger.info(f"[ZMQEyeInterface] set_brightness: brightness={brightness}, side={side}")
        try:
            self._send_command(cmd)
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] set_brightness failed: {e}")
            raise

    async def set_background(self, style: str, bg_type: str = "COLOR", side: str = 'BOTH', duration_ms: int = 0) -> None:
        """设置背景，支持指定侧别（LEFT/RIGHT/BOTH）和超时清除"""
        cmd = {
            "action": "set_background",
            "style": style,
            "type": bg_type,
            "side": side
        }
        
        # 如果指定了持续时间，添加到命令中
        if duration_ms > 0:
            cmd["duration_ms"] = duration_ms
            logger.info(f"[ZMQEyeInterface] set_background: style={style}, type={bg_type}, side={side}, duration={duration_ms}ms")
        else:
            logger.info(f"[ZMQEyeInterface] set_background: style={style}, type={bg_type}, side={side} (permanent)")
        
        try:
            self._send_command(cmd)
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] set_background failed: {e}")
            raise

    async def stop_animation(self) -> None:
        """停止当前眼睛动画"""
        # EyeEngine 似乎没有直接的 global stop action，这里发送一个空的或 reset
        cmd = {"action": "play_category", "category": "NORMAL"}
        
        logger.info(f"[ZMQEyeInterface] 停止动画 (通过播放 NORMAL 代替)")
        
        try:
            self._send_command(cmd)
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] 停止动画失败: {e}")
            raise

    async def stop_all(self) -> None:
        """停止所有动画和任务（清空队列）"""
        cmd = {"action": "stop"}
        
        logger.info(f"[ZMQEyeInterface] 停止所有任务")
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to stop all tasks: {response.get('error')}")
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] stop_all failed: {e}")
            raise

    async def blink(self, count: int = 1, side: str = "BOTH", duration: int = 200) -> None:
        """
        眨眼
        
        Args:
            count: 眨眼次数
            side: 显示屏幕（LEFT/RIGHT/BOTH）
            duration: 每次眨眼持续时间(ms)
        """
        cmd = {
            "action": "blink",
            "count": count,
            "side": side,
            "duration": duration
        }
        
        logger.info(f"[ZMQEyeInterface] blink: count={count}, side={side}, duration={duration}ms")
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to blink: {response.get('error')}")
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] blink failed: {e}")
            raise

    async def play_category(self, category: str, priority: int = 5) -> None:
        """
        播放指定分类的动画
        
        Args:
            category: 动画分类名称
            priority: 任务优先级（1-10，默认 5）
        """
        cmd = {
            "action": "play_category",
            "category": category
        }
        if self.priority_enabled:
            cmd["priority"] = priority
        
        logger.info(f"[ZMQEyeInterface] play_category: category={category}, priority={priority}")
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to play category: {response.get('error')}")
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] play_category failed: {e}")
            raise

    async def play_text_overlay(
        self, 
        text: str, 
        side: str = "BOTH",
        x: int = 120, 
        y: int = 120,
        font_size: int = 24, 
        color: str = "#FFFFFF",
        duration_ms: int = 3000
    ) -> Optional[str]:
        """
        在屏幕上叠加显示文字
        
        Args:
            text: 文字内容
            side: 显示屏幕（LEFT/RIGHT/BOTH）
            x: X 坐标
            y: Y 坐标
            font_size: 字体大小
            color: 文字颜色（十六进制）
            duration_ms: 持续时间(ms)
            
        Returns:
            overlay_id: 叠加层ID
        """
        cmd = {
            "action": "play_text_overlay",
            "text": text,
            "side": side,
            "x": x,
            "y": y,
            "font_size": font_size,
            "color": color,
            "duration_ms": duration_ms
        }
        
        logger.info(f"[ZMQEyeInterface] play_text_overlay: text='{text}', position=({x},{y}), duration={duration_ms}ms")
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to play text overlay: {response.get('error')}")
            return response.get('overlay_id') or response.get('task_id')
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] play_text_overlay failed: {e}")
            raise

    # ===== 查询方法 =====
    
    async def list_sequences(self) -> list:
        """列出所有可用的序列动画"""
        cmd = {"action": "list_sequences"}
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to list sequences: {response.get('error')}")
            return response.get("sequences", [])
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] list_sequences failed: {e}")
            raise

    async def list_behaviors(self) -> list:
        """列出所有行为动画"""
        cmd = {"action": "list_behaviors"}
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to list behaviors: {response.get('error')}")
            return response.get("behaviors", [])
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] list_behaviors failed: {e}")
            raise

    async def list_iris(self) -> Dict[str, list]:
        """列出所有虹膜样式"""
        cmd = {"action": "list_iris"}
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to list iris: {response.get('error')}")
            return response.get("iris", {})
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] list_iris failed: {e}")
            raise

    async def list_backgrounds(self) -> list:
        """列出所有背景样式"""
        cmd = {"action": "list_backgrounds"}
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to list backgrounds: {response.get('error')}")
            return response.get("backgrounds", [])
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] list_backgrounds failed: {e}")
            raise

    # ===== 可见性管理方法 =====
    
    async def show_widget(self, widget_id: str) -> None:
        """显示 widget，隐藏眼睛"""
        cmd = {
            "action": "show_widget",
            "widget_id": widget_id
        }
        
        logger.info(f"[ZMQEyeInterface] show_widget: widget_id={widget_id}")
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to show widget: {response.get('error')}")
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] show_widget failed: {e}")
            raise

    async def restore_eye(self) -> None:
        """恢复眼睛显示"""
        cmd = {"action": "restore_eye"}
        
        logger.info(f"[ZMQEyeInterface] restore_eye")
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to restore eye: {response.get('error')}")
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] restore_eye failed: {e}")
            raise

    async def pause_auto_restore(self) -> None:
        """暂停自动恢复眼睛显示"""
        cmd = {"action": "pause_auto_restore"}
        
        logger.info(f"[ZMQEyeInterface] pause_auto_restore")
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to pause auto restore: {response.get('error')}")
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] pause_auto_restore failed: {e}")
            raise

    async def resume_auto_restore(self) -> None:
        """恢复自动恢复眼睛显示"""
        cmd = {"action": "resume_auto_restore"}
        
        logger.info(f"[ZMQEyeInterface] resume_auto_restore")
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to resume auto restore: {response.get('error')}")
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] resume_auto_restore failed: {e}")
            raise

    async def set_manual_mode(self, enabled: bool) -> None:
        """设置手动模式（禁用自动行为）"""
        cmd = {
            "action": "set_manual_mode",
            "enabled": enabled
        }
        
        logger.info(f"[ZMQEyeInterface] set_manual_mode: enabled={enabled}")
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to set manual mode: {response.get('error')}")
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] set_manual_mode failed: {e}")
            raise

    async def get_visibility_status(self) -> Dict[str, Any]:
        """获取当前可见性状态"""
        cmd = {"action": "get_visibility_status"}
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to get visibility status: {response.get('error')}")
            return {
                "eye_visible": response.get("eye_visible", True),
                "widget_visible": response.get("widget_visible", False),
                "auto_restore_enabled": response.get("auto_restore_enabled", True)
            }
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] get_visibility_status failed: {e}")
            raise

    # ===== 视频流方法 =====
    
    async def enable_video_stream(self, resource_id: str) -> None:
        """启用摄像头视频流叠加"""
        cmd = {
            "action": "enable_video_stream",
            "resource_id": resource_id
        }
        
        logger.info(f"[ZMQEyeInterface] enable_video_stream: resource_id={resource_id}")
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to enable video stream: {response.get('error')}")
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] enable_video_stream failed: {e}")
            raise

    async def disable_video_stream(self) -> None:
        """禁用视频流"""
        cmd = {"action": "disable_video_stream"}
        
        logger.info(f"[ZMQEyeInterface] disable_video_stream")
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to disable video stream: {response.get('error')}")
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] disable_video_stream failed: {e}")
            raise

    async def video_stream_status(self) -> Dict[str, Any]:
        """获取视频流状态"""
        cmd = {"action": "video_stream_status"}
        
        try:
            response = self._send_command(cmd)
            if not response.get("success", False):
                raise RuntimeError(f"Failed to get video stream status: {response.get('error')}")
            return {
                "enabled": response.get("enabled", False),
                "resource_id": response.get("resource_id", "")
            }
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] video_stream_status failed: {e}")
            raise

    async def stop_animation(self) -> None:
        """停止当前眼睛动画"""
        # EyeEngine 似乎没有直接的 global stop action，这里发送一个空的或 reset
        cmd = {"action": "play_category", "category": "NORMAL"}
        
        logger.info(f"[ZMQEyeInterface] 停止动画 (通过播放 NORMAL 代替)")
        
        try:
            self._send_command(cmd)
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] 停止动画失败: {e}")
            raise

    async def play_sequence_animations(
        self,
        sequence: str,
        side: str = 'BOTH',
        loop: bool = False,
        loop_count: int = 0,
        fps: Optional[int] = None,
        speed: float = 1.0,
        delay_ms: Optional[int] = None,
        clear_time: int = 0,
        exclusive: bool = False
    ) -> Optional[str]:
        """
        在屏幕上叠加播放 .seq 动画（请求非阻塞启动）
        """
        cmd = {
            'action': 'play_sequence_animations',
            'sequence': sequence,
            'side': side,
            'loop': loop,
            'fps': fps,
            'speed': speed,
            'clear_time': clear_time,
            'exclusive': exclusive
        }
        if loop_count not in (None, 0):
            cmd['loop_count'] = loop_count
        if delay_ms is not None:
            cmd['delay_ms'] = int(delay_ms)

        try:
            response = self._send_command(cmd)
            if not response.get('success', False):
                raise RuntimeError(f"Failed to play overlay sequence: {response.get('error')}")
            # response for non-sync variant returns a task_id; return it as overlay_id substitute
            return response.get('overlay_id') or response.get('task_id')
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] play_sequence_animations failed: {e}")
            raise

    async def stop_overlay_sequence(self, overlay_id: str) -> bool:
        """
        停止指定的 overlay（同步）
        """
        cmd = {
            'action': 'stop_overlay_sequence_sync',
            'overlay_id': overlay_id
        }
        try:
            response = self._send_command(cmd)
            if not response.get('success', False):
                raise RuntimeError(f"Failed to stop overlay: {response.get('error')}")
            return True
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] stop_overlay_sequence failed: {e}")
            raise
    
    async def play_sprite_overlay(self, sprite_name: str, side: str = 'BOTH', loop: bool = False, fps: Optional[int] = None, scale: float = 1.0, rotate: float = 0.0, dx: int = 0, dy: int = 0, animate: Optional[str] = None, duration: Optional[float] = None) -> Optional[str]:
        """
        在屏幕上叠加播放单张 PNG 精灵（支持简单缩放/旋转/位移和可选动画）
        """
        cmd = {
            'action': 'play_sprite_overlay',
            'sprite': sprite_name,
            'side': side,
            'loop': loop,
            'fps': fps,
            'scale': scale,
            'rotate': rotate,
            'dx': dx,
            'dy': dy,
            'animate': animate,
            'duration': duration
        }
        try:
            response = self._send_command(cmd)
            if not response.get('success', False):
                raise RuntimeError(f"Failed to play sprite overlay: {response.get('error')}")
            return response.get('overlay_id') or response.get('task_id')
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] play_sprite_overlay failed: {e}")
            raise
    
    async def play_overlay_image(self, image: str, side: str = 'BOTH', loop: bool = False, fps: Optional[int] = None, x: int = 0, y: int = 0, scale: float = 1.0, rotation: float = 0.0, duration_ms: Optional[int] = None, delay_ms: Optional[int] = None) -> Optional[str]:
        """
        在屏幕上叠加播放一张 PNG 图片（可选简单动画）
        """
        cmd = {
            'action': 'play_overlay_image',
            'image': image,
            'side': side,
            'loop': loop,
            'fps': fps,
            'x': x,
            'y': y,
            'scale': scale,
            'rotation': rotation,
            'duration_ms': duration_ms,
            'delay_ms': delay_ms
        }

        try:
            response = self._send_command(cmd)
            if not response.get('success', False):
                raise RuntimeError(f"Failed to play overlay image: {response.get('error')}")
            return response.get('overlay_id') or response.get('task_id')
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] play_overlay_image failed: {e}")
            raise

    async def stop_overlay_image(self, overlay_id: str) -> bool:
        """
        停止指定的图片 overlay（同步）
        """
        cmd = {
            'action': 'stop_overlay_image_sync',
            'overlay_id': overlay_id
        }
        try:
            response = self._send_command(cmd)
            if not response.get('success', False):
                raise RuntimeError(f"Failed to stop overlay image: {response.get('error')}")
            return True
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] stop_overlay_image failed: {e}")
            raise
    
    def set_priority_enabled(self, enabled: bool) -> bool:
        """设置远端 EyeEngine 的优先级开关，并在本地缓存结果
        Returns True 如果命令发送并成功返回，否则 False
        """
        try:
            cmd = {"action": "set_priority_enabled", "enabled": bool(enabled)}
            resp = self._send_command(cmd)
            ok = bool(resp.get("success", False))
            self.priority_enabled = bool(enabled)
            if ok:
                logger.info(f"[ZMQEyeInterface] Priority system {'enabled' if enabled else 'disabled'} on EyeEngine")
            else:
                logger.warning(f"[ZMQEyeInterface] Failed to set priority_enabled on EyeEngine: {resp}")
            return ok
        except Exception as e:
            logger.warning(f"[ZMQEyeInterface] set_priority_enabled failed: {e}")
            # 即使远端设置失败，也在本地更新标志以改变本地行为
            self.priority_enabled = bool(enabled)
            return False

    def get_priority_status(self) -> Dict[str, Any]:
        """获取 EyeEngine 的优先级占用/队列快照"""
        try:
            resp = self._send_command({"action": "priority_status"})
            return resp
        except Exception as e:
            logger.error(f"[ZMQEyeInterface] get_priority_status failed: {e}")
            return {"success": False, "error": str(e)}
    
    def close(self) -> None:
        """关闭连接"""
        logger.info(f"[ZMQEyeInterface] 关闭连接")
        self._cleanup()
    
    def __del__(self):
        """析构函数"""
        self.close()
