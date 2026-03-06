"""
LED 系统接口实现

通过 ZMQ 向 Drive 系统发送控制指令

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import zmq
import json
import logging
import asyncio
from typing import Optional

from ..hardware_interfaces import LEDInterface

logger = logging.getLogger(__name__)


class ZmqLEDInterface(LEDInterface):
    """通过 ZMQ 控制 LED 的接口实现"""
    
    def __init__(
        self,
        socket_path: str = "ipc:///tmp/doly_control.sock",
        debug: bool = False
    ):
        """
        初始化 Zmq LED 接口
        
        Args:
            socket_path: ZMQ Socket 路径
            debug: 是否启用调试日志
        """
        self.debug = debug
        self.socket_path = socket_path
        self.ctx = zmq.Context.instance()
        self.socket = self.ctx.socket(zmq.PUSH)
        # 增加高水位限制，防止堆积
        self.socket.setsockopt(zmq.SNDHWM, 100)
        
        try:
            self.socket.connect(self.socket_path)
            logger.info(f"[ZmqLEDInterface] 已连接到 {socket_path}")
        except Exception as e:
            logger.error(f"[ZmqLEDInterface] 连接失败: {e}")
            self.socket = None

    async def _send_command(self, action: str, params: dict) -> bool:
        """发送命令到硬件"""
        if not self.socket:
            return False
            
        try:
            topic = "io.pca9535.control"
            cmd = {"action": action}
            cmd.update(params)
            
            # 使用同步方法发送，因为是 PUSH socket 且在本地
            self.socket.send_string(topic, zmq.SNDMORE)
            self.socket.send_string(json.dumps(cmd))
            
            if self.debug:
                logger.debug(f"[ZmqLEDInterface] 发送命令: {cmd}")
            # ZMQ PUSH socket 非阻塞发送，无需额外 sleep - 这能加快执行速度
            return True
        except Exception as e:
            logger.error(f"[ZmqLEDInterface] 发送命令失败: {e}")
            return False

    async def set_color(self, color: str, side: int = 0, duration_ms: int = 0,
                       default_color: str = '#000000') -> None:
        """
        设置 LED 静态颜色
        
        Args:
            color: 颜色值（十六进制格式，如 '#ffffff' 或 RGB '255,100,0'）
            side: 0 - 全部, 1 - 左, 2 - 右
            duration_ms: 显示持续时间（毫秒，0表示永久）
            default_color: 未使用（保留用于未来扩展）
        """
        r, g, b = self._parse_color(color)
        
        side_map = {0: "BOTH", 1: "LEFT", 2: "RIGHT"}
        side_str = side_map.get(side, "BOTH")
        
        command = {
            "r": r, "g": g, "b": b, "side": side_str
        }
        
        # 如果指定了显示时长，则让底层自动超时停止
        if duration_ms > 0:
            command["duration_ms"] = duration_ms
        
        duration_str = f", duration={duration_ms}ms" if duration_ms > 0 else ""
        # logger.info(f"[ZmqLEDInterface] 设置 LED 颜色 {side_str}: RGB({r},{g},{b}){duration_str}")
        
        await self._send_command("set_led_color", command)
        
        # 如果指定了显示时间，则等待该时长
        if duration_ms > 0:
            await asyncio.sleep(duration_ms / 1000.0)

    async def set_color_with_fade(
        self, 
        color: str, 
        duration_ms: int,
        fade_color: Optional[str] = None,
        side: int = 0,
        default_color: str = '#000000'
    ) -> None:
        """
        设置 LED 颜色并可选渐变 (映射为呼吸效果)
        """
        r, g, b = self._parse_color(color)
        side_map = {0: "BOTH", 1: "LEFT", 2: "RIGHT"}
        side_str = side_map.get(side, "BOTH")
        
        # 将时长映射为速度 (简单转换)
        speed = max(1, min(100, 100 - (duration_ms // 100)))
        
        command = {
            "effect": "breath",
            "r": r, "g": g, "b": b,
            "speed": speed,
            "side": side_str,
            "duration_ms": duration_ms  # 传递持续时间到底层，超时后自动停止
        }
        
        logger.info(f"[ZmqLEDInterface] LED 呼吸效果 {side_str}: RGB({r},{g},{b}), speed={speed}, duration={duration_ms}ms")
        
        await self._send_command("set_led_effect", command)
        
        # 如果指定了渐变时间，则等待该时长
        if duration_ms > 0:
            await asyncio.sleep(duration_ms / 1000.0)

    async def turn_off(self, side: int = 0) -> None:
        """关闭 LED"""
        side_map = {0: "BOTH", 1: "LEFT", 2: "RIGHT"}
        side_str = side_map.get(side, "BOTH")
        await self._send_command("led_off", {"side": side_str})

    def _parse_color(self, color_str: str) -> tuple:
        """解析颜色字符串为 RGB"""
        try:
            if color_str.startswith('#'):
                color_str = color_str.lstrip('#')
                return tuple(int(color_str[i:i+2], 16) for i in (0, 2, 4))
            elif ',' in color_str:
                return tuple(map(int, color_str.split(',')))
        except:
            return (255, 255, 255)
        return (255, 255, 255)


# 为兼容旧代码，保留别名
DolyCLEDInterface = ZmqLEDInterface
