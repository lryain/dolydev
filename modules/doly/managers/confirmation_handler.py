"""
异步确认处理器

用于人脸管理等需要用户确认的操作，支持超时和取消机制。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import asyncio
import logging
import time
import threading  # 添加缺失的 threading 导入
from typing import Optional, Callable, Dict, Any
from enum import Enum

logger = logging.getLogger(__name__)


class ConfirmationState(Enum):
    """确认状态"""
    IDLE = "idle"                    # 空闲
    WAITING = "waiting"              # 等待确认
    CONFIRMED = "confirmed"          # 已确认
    CANCELLED = "cancelled"          # 已取消
    TIMEOUT = "timeout"              # 超时


class ConfirmationHandler:
    """
    异步确认处理器
    
    负责管理需要用户确认的操作流程，包括:
    - 发起确认请求
    - 等待用户响应（确认/取消）
    - 超时处理
    - 执行回调
    
    Example:
        ```python
        handler = ConfirmationHandler(tts_client=tts)
        
        # 请求确认
        await handler.request_confirmation(
            operation_type='register_face',
            operation_data={'name': 'Alice', 'feature': [...]},
            prompt='准备注册新人脸，请说确认',
            callback=execute_register,
            timeout=30.0
        )
        
        # 用户响应
        handler.handle_response(confirmed=True)  # 或 False
        ```
    """
    
    def __init__(self, tts_client=None):
        """
        初始化确认处理器
        
        Args:
            tts_client: TTS 客户端（用于播报提示）
        """
        self.tts_client = tts_client
        self.state = ConfirmationState.IDLE
        self.pending_operation: Optional[Dict[str, Any]] = None
        self.timeout_task: Optional[asyncio.Task] = None
        self.callback: Optional[Callable] = None
        self.request_time: float = 0
        
        # 统计信息
        self.stats = {
            'total_requests': 0,
            'confirmed_count': 0,
            'cancelled_count': 0,
            'timeout_count': 0
        }
        
        logger.info("✅ ConfirmationHandler 初始化完成")
    
    def request_confirmation(
        self,
        operation_type: str,
        operation_data: Dict[str, Any],
        prompt: str,
        callback: Callable,
        timeout: float = 30.0,
        tts_config: Optional[Dict[str, Any]] = None
    ) -> bool:
        """
        请求用户确认（同步方法，使用线程Timer实现超时）
        
        Args:
            operation_type: 操作类型 (register, update, delete)
            operation_data: 操作数据（将传递给回调函数）
            prompt: TTS 提示文本
            callback: 确认后的回调函数
            timeout: 超时时间（秒）
            tts_config: TTS 配置（可选）
            
        Returns:
            是否成功发起确认流程
        """
        # 检查状态
        if self.state != ConfirmationState.IDLE:
            logger.warning(f"⚠️ 当前有待处理的确认请求（state={self.state.value}），无法发起新请求")
            if self.tts_client:
                self.tts_client.speak("当前有待处理的操作，请稍后再试")
            return False
        
        # 设置状态
        self.state = ConfirmationState.WAITING
        self.pending_operation = {
            'type': operation_type,
            'data': operation_data
        }
        self.callback = callback
        self.request_time = time.time()
        self.stats['total_requests'] += 1
        
        # TTS 提示
        if self.tts_client and prompt:
            try:
                # 使用自定义 TTS 配置（如果提供）
                if tts_config:
                    self.tts_client.speak(prompt, **tts_config)
                else:
                    self.tts_client.speak(prompt)
                logger.info(f"🔊 TTS 提示: {prompt}")
            except Exception as e:
                logger.error(f"❌ TTS 播报失败: {e}", exc_info=True)
        
        # 启动超时任务（使用线程Timer）
        if timeout > 0:
            self.timeout_task = threading.Timer(timeout, self._timeout_handler_sync)
            self.timeout_task.daemon = True
            self.timeout_task.start()
            logger.info(f"⏳ 等待用户确认: {operation_type}, 超时: {timeout}s")
        else:
            logger.info(f"⏳ 等待用户确认: {operation_type} (无超时)")
        
        return True
    
    def _timeout_handler_sync(self):
        """
        超时处理（同步版本，在Timer线程中执行）
        """
        # 检查是否仍在等待状态
        if self.state == ConfirmationState.WAITING:
            elapsed = time.time() - self.request_time
            logger.warning(f"⏱️ 确认超时 ({elapsed:.1f}s)，自动取消操作")
            self.state = ConfirmationState.TIMEOUT
            self.stats['timeout_count'] += 1
            
            # TTS 提示
            if self.tts_client:
                try:
                    self.tts_client.speak("操作超时，已自动取消")
                except Exception as e:
                    logger.error(f"❌ TTS 超时提示失败: {e}")
            
            # 重置状态
            self._reset()
    
    def handle_response(self, confirmed: bool) -> bool:
        """
        处理用户响应
        
        Args:
            confirmed: True=确认, False=取消
            
        Returns:
            是否成功处理
        """
        if self.state != ConfirmationState.WAITING:
            logger.warning(f"⚠️ 当前无待确认操作（state={self.state.value}），忽略响应")
            return False
        
        # 计算响应时间
        response_time = time.time() - self.request_time
        
        # 取消超时任务（Timer 版本）
        if self.timeout_task and isinstance(self.timeout_task, threading.Timer):
            if self.timeout_task.is_alive():
                self.timeout_task.cancel()
                logger.debug("⏹️ 已取消超时计时器")
        
        if confirmed:
            logger.info(f"✅ 用户确认操作（响应时间: {response_time:.1f}s）")
            self.state = ConfirmationState.CONFIRMED
            self.stats['confirmed_count'] += 1
            
            # 执行回调
            if self.callback and self.pending_operation:
                try:
                    logger.debug(f"🔄 执行回调: {self.pending_operation['type']}")
                    result = self.callback(self.pending_operation)
                    logger.info(f"✅ 操作执行{'成功' if result else '失败'}")
                    return result
                except Exception as e:
                    logger.error(f"❌ 执行操作异常: {e}", exc_info=True)
                    
                    # TTS 错误提示
                    if self.tts_client:
                        self.tts_client.speak("操作执行失败，请重试")
                    
                    return False
                finally:
                    self._reset()
            else:
                logger.error("❌ 回调函数或操作数据缺失")
                self._reset()
                return False
        else:
            logger.info(f"❌ 用户取消操作（响应时间: {response_time:.1f}s）")
            self.state = ConfirmationState.CANCELLED
            self.stats['cancelled_count'] += 1
            
            # TTS 提示
            if self.tts_client:
                self.tts_client.speak("操作已取消")
            
            self._reset()
            return True
    
    def _reset(self):
        """重置状态"""
        logger.debug("🔄 重置确认处理器状态")
        self.state = ConfirmationState.IDLE
        self.pending_operation = None
        self.callback = None
        self.timeout_task = None
        self.request_time = 0
    
    def get_state(self) -> ConfirmationState:
        """获取当前状态"""
        return self.state
    
    def is_waiting(self) -> bool:
        """是否正在等待确认"""
        return self.state == ConfirmationState.WAITING
    
    def get_pending_operation(self) -> Optional[Dict[str, Any]]:
        """获取待处理的操作"""
        return self.pending_operation
    
    def get_stats(self) -> Dict[str, int]:
        """
        获取统计信息
        
        Returns:
            统计数据字典
        """
        return self.stats.copy()
    
    def cancel_current(self):
        """取消当前待处理的确认请求"""
        if self.state == ConfirmationState.WAITING:
            logger.info("🚫 强制取消当前确认请求")
            
            # 取消超时任务
            if self.timeout_task and not self.timeout_task.done():
                self.timeout_task.cancel()
            
            self.state = ConfirmationState.CANCELLED
            self.stats['cancelled_count'] += 1
            
            # TTS 提示
            if self.tts_client:
                self.tts_client.speak("操作已取消")
            
            self._reset()
        else:
            logger.warning(f"⚠️ 当前无待确认操作（state={self.state.value}），无法取消")


# 辅助函数

def create_confirmation_handler(tts_client=None) -> ConfirmationHandler:
    """
    工厂函数：创建确认处理器实例
    
    Args:
        tts_client: TTS 客户端
        
    Returns:
        ConfirmationHandler 实例
    """
    return ConfirmationHandler(tts_client=tts_client)
