"""
EyeEngine ZMQ 客户端

封装与 eyeEngine 服务的所有 ZMQ 通信

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import zmq
import logging
from typing import Optional, Dict, Any

logger = logging.getLogger(__name__)


class EyeEngineClient:
    """eyeEngine ZMQ 客户端"""
    
    def __init__(self, endpoint: str = "ipc:///tmp/doly_eye_cmd.sock", timeout: int = 5000):
        """
        初始化 eyeEngine 客户端
        
        Args:
            endpoint: ZMQ 端点地址
            timeout: 超时时间(毫秒)
        """
        self.endpoint = endpoint
        self.timeout = timeout
        self.ctx = zmq.Context.instance()
        self.socket = None
        self._connected = False
        
        logger.info(f"EyeEngineClient 初始化: {endpoint}")
    
    def connect(self) -> bool:
        """连接到 eyeEngine 服务"""
        try:
            if self.socket:
                self.socket.close()
            
            self.socket = self.ctx.socket(zmq.REQ)
            self.socket.setsockopt(zmq.RCVTIMEO, self.timeout)
            self.socket.setsockopt(zmq.SNDTIMEO, self.timeout)
            self.socket.connect(self.endpoint)
            
            self._connected = True
            logger.info(f"已连接到 eyeEngine: {self.endpoint}")
            return True
        except Exception as e:
            logger.error(f"连接 eyeEngine 失败: {e}")
            self._connected = False
            return False
    
    def _send_command(self, action: str, **params) -> Optional[Dict[str, Any]]:
        """
        发送命令到 eyeEngine
        
        Args:
            action: 动作名称
            **params: 命令参数
            
        Returns:
            响应字典，失败返回 None
        """
        if not self._connected:
            if not self.connect():
                return None
        
        try:
            cmd = {"action": action}
            cmd.update(params)
            
            self.socket.send_json(cmd)
            response = self.socket.recv_json()
            
            if not response.get('success'):
                logger.warning(f"eyeEngine 命令失败: {action}, error={response.get('error')}")
            
            return response
        except zmq.Again:
            logger.error(f"eyeEngine 命令超时: {action}")
            self._connected = False
            return None
        except Exception as e:
            logger.error(f"eyeEngine 命令异常: {action}, error={e}")
            self._connected = False
            return None
    
    # ===== Visibility Management =====
    
    def show_widget(self, widget_name: str, timeout: Optional[int] = None) -> bool:
        """
        显示 widget (隐藏 eye)
        
        Args:
            widget_name: widget 名称
            timeout: 超时时间(秒), None=使用默认, 0=禁用
            
        Returns:
            是否成功
        """
        params = {"widget": widget_name}
        if timeout is not None:
            params["timeout"] = timeout
        
        response = self._send_command("show_widget", **params)
        return response and response.get('success', False)
    
    def restore_eye(self, reason: str = "manual") -> bool:
        """
        恢复 eye 显示 (隐藏 widget)
        
        Args:
            reason: 恢复原因
            
        Returns:
            是否成功
        """
        response = self._send_command("restore_eye", reason=reason)
        return response and response.get('success', False)
    
    def pause_auto_restore(self) -> bool:
        """暂停自动恢复"""
        response = self._send_command("pause_auto_restore")
        return response and response.get('success', False)
    
    def resume_auto_restore(self, timeout: Optional[int] = None) -> bool:
        """恢复自动恢复"""
        params = {}
        if timeout is not None:
            params["timeout"] = timeout
        response = self._send_command("resume_auto_restore", **params)
        return response and response.get('success', False)
    
    def set_manual_mode(self, enabled: bool) -> bool:
        """设置手动模式"""
        response = self._send_command("set_manual_mode", enabled=enabled)
        return response and response.get('success', False)
    
    def get_visibility_status(self) -> Optional[Dict[str, Any]]:
        """获取显示状态"""
        response = self._send_command("get_visibility_status")
        if response and response.get('success'):
            return response.get('status')
        return None
    
    # ===== Animation Control =====
    
    def play_animation(self, category: str, animation: Optional[str] = None, 
                      priority: int = 5, hold_duration: float = 0.0) -> bool:
        """
        播放动画
        
        Args:
            category: 动画分类
            animation: 动画名称(可选)
            priority: 优先级
            hold_duration: 保持时长(秒)
            
        Returns:
            是否成功提交
        """
        params = {"category": category, "priority": priority, "hold_duration": hold_duration}
        if animation:
            params["animation"] = animation
        
        response = self._send_command("play_animation", **params)
        return response and response.get('success', False)
    
    def play_behavior(self, animation_file: str, priority: int = 5) -> bool:
        """
        播放行为动画
        
        Args:
            animation_file: 动画文件名
            priority: 优先级
            
        Returns:
            是否成功
        """
        response = self._send_command("play_behavior", animation=animation_file, priority=priority)
        return response and response.get('success', False)
    
    def blink(self, animation: Optional[str] = None, priority: int = 5) -> bool:
        """
        眨眼
        
        Args:
            animation: 眨眼动画名称(可选)
            priority: 优先级
            
        Returns:
            是否成功
        """
        params = {"priority": priority}
        if animation:
            params["animation"] = animation
        response = self._send_command("blink", **params)
        return response and response.get('success', False)
    
    def stop(self) -> bool:
        """停止当前动画"""
        response = self._send_command("stop")
        return response and response.get('success', False)
    
    # ===== Eye Appearance =====
    
    def set_iris(self, theme: Optional[str] = None, style: Optional[str] = None,
                side: str = "BOTH", priority: int = 5) -> bool:
        """
        设置虹膜
        
        Args:
            theme: 虹膜主题
            style: 虹膜样式
            side: 眼睛侧面(LEFT/RIGHT/BOTH)
            priority: 优先级
            
        Returns:
            是否成功
        """
        params = {"side": side, "priority": priority}
        if theme:
            params["theme"] = theme
        if style:
            params["style"] = style
        
        response = self._send_command("set_iris", **params)
        return response and response.get('success', False)
    
    def set_background(self, bg_type: str, style: str, priority: int = 5) -> bool:
        """
        设置背景
        
        Args:
            bg_type: 背景类型(COLOR/IMAGE)
            style: 背景样式
            priority: 优先级
            
        Returns:
            是否成功
        """
        response = self._send_command("set_background", type=bg_type, style=style, priority=priority)
        return response and response.get('success', False)
    
    def set_brightness(self, value: int, side: str = "BOTH", priority: int = 5) -> bool:
        """
        设置亮度
        
        Args:
            value: 亮度值(0-10)
            side: 眼睛侧面(LEFT/RIGHT/BOTH)
            priority: 优先级
            
        Returns:
            是否成功
        """
        response = self._send_command("set_brightness", value=value, side=side, priority=priority)
        return response and response.get('success', False)
    
    def set_gaze(self, x: float, y: float, duration: int = 300, priority: int = 5) -> bool:
        """
        设置眼神注视位置（眼神跟随）
        
        Args:
            x: 水平位置(-1.0 到 1.0，-1=左，1=右)
            y: 垂直位置(-1.0 到 1.0，-1=下，1=上)
            duration: 移动时长(毫秒)
            priority: 优先级
            
        Returns:
            是否成功
        """
        response = self._send_command("set_gaze", x=x, y=y, duration=duration, priority=priority)
        return response and response.get('success', False)
    
    # ===== Overlay Image =====
    
    def play_overlay_image(self, image: str, delay_ms: int = 3000, 
                          side: str = "RIGHT", scale: float = 0.3) -> bool:
        """
        播放叠加图片（异步）
        
        Args:
            image: 图片文件路径
            delay_ms: 显示时长(毫秒)
            side: 显示侧面(LEFT/RIGHT)
            scale: 缩放比例
            
        Returns:
            是否成功
        """
        response = self._send_command("play_overlay_image", 
                                     image=image, 
                                     delay_ms=delay_ms, 
                                     side=side, 
                                     scale=scale)
        return response and response.get('success', False)
    
    def play_overlay_image_sync(self, image: str, delay_ms: int = 3000, 
                               side: str = "RIGHT", scale: float = 0.3) -> bool:
        """
        播放叠加图片（同步）
        
        Args:
            image: 图片文件路径
            delay_ms: 显示时长(毫秒)
            side: 显示侧面(LEFT/RIGHT)
            scale: 缩放比例
            
        Returns:
            是否成功
        """
        response = self._send_command("play_overlay_image_sync", 
                                     image=image, 
                                     delay_ms=delay_ms, 
                                     side=side, 
                                     scale=scale)
        return response and response.get('success', False)
    
    # ===== Status =====
    
    def get_status(self) -> Optional[Dict[str, Any]]:
        """获取 eyeEngine 状态"""
        response = self._send_command("get_status")
        if response and response.get('success'):
            return response.get('status')
        return None
    
    def ping(self) -> bool:
        """Ping eyeEngine 服务"""
        response = self._send_command("ping")
        return response and response.get('success', False)
    
    # ===== Cleanup =====
    
    def close(self):
        """关闭连接"""
        if self.socket:
            self.socket.close()
            self.socket = None
            self._connected = False
            logger.info("EyeEngineClient 已关闭")
