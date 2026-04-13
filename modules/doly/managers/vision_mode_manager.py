"""
Vision Service 模式管理器

负责通过 EventBus 的 ZMQ Publisher 与 Vision Service 通信，切换运行模式（IDLE/DETECT_TRACK/FULL）

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import json
import time
import logging
import threading
from enum import Enum
from typing import Union, Dict, Any, Optional

logger = logging.getLogger(__name__)


class VisionMode(Enum):
    """Vision Service 运行模式"""
    IDLE = "IDLE"                # 空载模式（最低资源消耗）
    STREAM_ONLY = "STREAM_ONLY"  # 仅推流/采集，不做检测识别
    DETECT_TRACK = "DETECT_TRACK"  # 检测+跟踪模式
    FULL = "FULL"                  # 全功能模式


class VisionModeManager:
    """
    Vision Service 模式管理器
    
    职责：
    - 通过 EventBus 的 ZMQ Publisher 发送模式切换命令
    - 管理模式超时自动回 IDLE
    - 统计模式切换次数
    
    Note: 不再自己管理 ZMQ socket，而是通过依赖注入接收 zmq_publisher
    """
    
    def __init__(self, 
                 zmq_publisher=None,
                 default_timeout: int = 30):
        """
        初始化模式管理器
        
        Args:
            zmq_publisher: ZMQ PUB socket（由 EventBus 提供）
            default_timeout: 默认超时时间（秒），0 表示不超时
        """
        self.zmq_publisher = zmq_publisher
        self.default_timeout = default_timeout
        
        # 超时定时器
        self._timeout_timer: Optional[threading.Timer] = None
        
        # 统计信息
        self._stats = {
            'mode_switches': 0,
            'timeouts': 0,
            'errors': 0,
            'last_mode': None,
            'last_switch_time': None
        }
        
        logger.info(f"[VisionModeManager] 初始化完成: default_timeout={default_timeout}s")
    
    def set_zmq_publisher(self, publisher) -> None:
        """
        设置 ZMQ Publisher（依赖注入）
        
        Args:
            publisher: EventBus 的 _zmq_pub socket
        """
        self.zmq_publisher = publisher
        logger.info("[VisionModeManager] ZMQ Publisher 已设置")
    
    def set_mode(self, 
                 mode: Union[str, VisionMode], 
                 timeout: int = 0,
                 repeat: int = 1) -> bool:
        """
        切换 Vision Service 运行模式
        
        Args:
            mode: 目标模式（IDLE/STREAM_ONLY/DETECT_TRACK/FULL 或对应的字符串）
            timeout: 超时自动回 IDLE（秒），0 表示使用默认超时，-1 表示永不超时
            repeat: 发送次数（用于确保在连接建立初期消息不丢失）
            
        Returns:
            是否成功发送命令
        """
        # 解析模式
        if isinstance(mode, str):
            try:
                target_mode = VisionMode[mode.upper()]
            except KeyError:
                logger.error(f"[VisionModeManager] ❌ 无效模式: {mode}")
                self._stats['errors'] += 1
                return False
        else:
            target_mode = mode
        
        # 发送模式切换命令
        success = False
        for i in range(repeat):
            if i > 0:
                time.sleep(0.1) # 100ms interval between repeats
            if self._send_mode_command(target_mode):
                success = True
        
        if success:
            # 更新统计
            self._stats['mode_switches'] += 1
            self._stats['last_mode'] = target_mode.value
            self._stats['last_switch_time'] = time.time()
            
            # 取消现有超时计时器
            self.cancel_timeout()
            
            # 启动新的超时计时器（如果需要）
            actual_timeout = timeout if timeout != 0 else self.default_timeout
            if actual_timeout > 0:
                self._timeout_timer = threading.Timer(
                    actual_timeout,
                    self._on_timeout
                )
                self._timeout_timer.daemon = True
                self._timeout_timer.start()
                logger.info(f"[VisionModeManager] ⏱️  启动超时计时器: {actual_timeout}秒后自动回 IDLE")
        
        return success
    
    def _send_mode_command(self, mode: VisionMode) -> bool:
        """
        通过 EventBus 的 ZMQ Publisher 发送模式切换命令
        
        Args:
            mode: 目标模式
            
        Returns:
            是否成功发送
        """
        if not self.zmq_publisher:
            logger.error("[VisionModeManager] ❌ ZMQ Publisher 未设置，无法发送命令！")
            self._stats['errors'] += 1
            return False
        
        try:
            # 构造命令消息（遵循 FaceReco 通信协议）
            topic = "cmd.vision.mode"
            message = {
                "ts": int(time.time() * 1000),
                "src": "doly_daemon",
                "version": "1.0",
                "data": {
                    "mode": mode.value
                }
            }
            
            # 通过 EventBus 的 ZMQ PUB socket 发送
            payload_json = json.dumps(message)
            self.zmq_publisher.send_multipart([
                topic.encode('utf-8'),
                payload_json.encode('utf-8')
            ])
            
            # 增强日志：记录实际发送的完整数据
            logger.info(f"[VisionModeManager] ✅ 模式命令已发送: topic={topic}, mode={mode.value}")
            logger.debug(f"[VisionModeManager] 📦 发送数据: {payload_json}")
            return True
            
        except Exception as e:
            logger.error(f"[VisionModeManager] ❌ 发送命令失败: {e}", exc_info=True)
            self._stats['errors'] += 1
            return False
    
    def _on_timeout(self) -> None:
        """
        超时回调：自动切换回 IDLE 模式
        """
        logger.info("[VisionModeManager] ⏰ 超时触发，自动切换回 IDLE 模式")
        self._stats['timeouts'] += 1
        self.set_mode(VisionMode.IDLE, timeout=-1)  # -1 表示永不超时
    
    def cancel_timeout(self) -> None:
        """取消超时计时器"""
        if self._timeout_timer:
            self._timeout_timer.cancel()
            self._timeout_timer = None
            logger.debug("[VisionModeManager] 超时计时器已取消")
    
    def get_stats(self) -> Dict[str, Any]:
        """
        获取统计信息
        
        Returns:
            统计信息字典
        """
        return self._stats.copy()
