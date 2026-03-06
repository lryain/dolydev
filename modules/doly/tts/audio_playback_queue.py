"""
TTS音频播放队列管理器

负责管理TTS合成的音频文件播放队列，并通过ZMQ与audio_player服务通信

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import zmq
import json
import time
import logging
import threading
from pathlib import Path
from typing import Optional, Dict, Any, Callable
from queue import Queue, Full, Empty
from dataclasses import dataclass, field

logger = logging.getLogger(__name__)


@dataclass
class AudioPlaybackTask:
    """音频播放任务"""
    audio_file: Path
    priority: int = 50  # 0-100, 数值越大优先级越高
    ducking: bool = True  # 是否启用背景音乐降低音量
    callback: Optional[Callable] = None  # 播放完成回调
    metadata: Dict[str, Any] = field(default_factory=dict)  # 额外元数据
    timestamp: float = field(default_factory=time.time)  # 创建时间


class AudioPlaybackQueue:
    """
    TTS音频播放队列管理器
    
    功能:
    - 音频播放任务队列管理
    - 与audio_player服务ZMQ通信
    - 播放状态监控
    - 多种队列模式(FIFO, 优先级, 立即打断)
    """
    
    def __init__(self, config: Dict[str, Any]):
        """
        初始化播放队列
        
        Args:
            config: 队列配置字典
        """
        self.config = config
        self.enabled = config.get('enabled', True)
        self.max_size = config.get('max_size', 10)
        self.mode = config.get('mode', 'fifo')
        self.wait_completion = config.get('wait_completion', False)
        self.default_priority = config.get('default_priority', 50)
        self.enable_ducking = config.get('enable_ducking', True)
        self.timeout = config.get('timeout', 30)
        
        # ZMQ端点
        self.cmd_endpoint = config.get('audio_player_cmd', 'ipc:///tmp/doly_audio_player_cmd.sock')
        self.status_endpoint = config.get('audio_player_status', 'ipc:///tmp/doly_audio_player_status.sock')
        
        # 队列
        self.task_queue = Queue(maxsize=self.max_size)
        
        # ZMQ上下文和socket
        self.zmq_context = None
        self.cmd_socket = None
        self.status_socket = None
        
        # 线程控制
        self._running = False
        self._worker_thread = None
        self._status_thread = None
        
        # 当前播放状态
        self.current_task: Optional[AudioPlaybackTask] = None
        self.is_playing = False
        
        if self.enabled:
            self._init_zmq()
            self._start_workers()
            logger.info("[AudioPlaybackQueue] 初始化完成")
        else:
            logger.info("[AudioPlaybackQueue] 队列功能已禁用")
    
    def _init_zmq(self):
        """初始化ZMQ连接"""
        try:
            self.zmq_context = zmq.Context()
            
            # 命令socket (REQ模式)
            self.cmd_socket = self.zmq_context.socket(zmq.REQ)
            self.cmd_socket.connect(self.cmd_endpoint)
            self.cmd_socket.setsockopt(zmq.LINGER, 0)
            self.cmd_socket.setsockopt(zmq.RCVTIMEO, 5000)  # 5秒超时
            
            # 状态订阅socket (SUB模式)
            self.status_socket = self.zmq_context.socket(zmq.SUB)
            self.status_socket.connect(self.status_endpoint)
            self.status_socket.setsockopt_string(zmq.SUBSCRIBE, 'status.audio.playback')
            self.status_socket.setsockopt(zmq.RCVTIMEO, 1000)  # 1秒超时
            
            logger.info(f"[AudioPlaybackQueue] ZMQ连接成功: cmd={self.cmd_endpoint}, status={self.status_endpoint}")
            
        except Exception as e:
            logger.error(f"[AudioPlaybackQueue] ZMQ初始化失败: {e}", exc_info=True)
            self.enabled = False
    
    def _start_workers(self):
        """启动工作线程"""
        self._running = True
        
        # 播放任务处理线程
        self._worker_thread = threading.Thread(target=self._worker_loop, daemon=True, name="TTSPlaybackWorker")
        self._worker_thread.start()
        
        # 状态监控线程
        self._status_thread = threading.Thread(target=self._status_loop, daemon=True, name="TTSStatusMonitor")
        self._status_thread.start()
        
        logger.info("[AudioPlaybackQueue] 工作线程已启动")
    
    def enqueue(
        self, 
        audio_file: Path,
        priority: Optional[int] = None,
        ducking: Optional[bool] = None,
        callback: Optional[Callable] = None,
        metadata: Optional[Dict] = None
    ) -> bool:
        """
        添加音频播放任务到队列
        
        Args:
            audio_file: 音频文件路径
            priority: 优先级(0-100)
            ducking: 是否启用背景音乐降低音量
            callback: 播放完成回调函数
            metadata: 额外元数据
            
        Returns:
            是否成功加入队列
        """
        if not self.enabled:
            logger.warning("[AudioPlaybackQueue] 队列已禁用，无法添加任务")
            return False
        
        if not audio_file.exists():
            logger.error(f"[AudioPlaybackQueue] 音频文件不存在: {audio_file}")
            return False
        
        # 创建任务
        task = AudioPlaybackTask(
            audio_file=audio_file,
            priority=priority if priority is not None else self.default_priority,
            ducking=ducking if ducking is not None else self.enable_ducking,
            callback=callback,
            metadata=metadata or {}
        )
        
        # 根据模式处理队列
        if self.mode == 'immediate':
            # 立即打断模式：清空队列并停止当前播放
            self._clear_queue()
            self._stop_current()
        
        try:
            if self.mode == 'priority':
                # 优先级模式：需要重新排序（这里简化为FIFO）
                # 实际应该使用PriorityQueue
                logger.warning("[AudioPlaybackQueue] 优先级模式暂未完全实现，使用FIFO")
            
            # 尝试加入队列
            self.task_queue.put(task, block=False)
            logger.info(f"[AudioPlaybackQueue] 任务已入队: {audio_file.name}, priority={task.priority}, queue_size={self.task_queue.qsize()}")
            return True
            
        except Full:
            logger.error(f"[AudioPlaybackQueue] 队列已满({self.max_size})，任务被拒绝: {audio_file.name}")
            return False
    
    def _worker_loop(self):
        """工作线程主循环：从队列取任务并播放"""
        logger.info("[AudioPlaybackQueue] Worker线程启动")
        
        while self._running:
            try:
                # 从队列获取任务
                task = self.task_queue.get(timeout=0.5)
                
                logger.info(f"[AudioPlaybackQueue] 开始处理任务: {task.audio_file.name}")
                
                # 设置当前任务
                self.current_task = task
                
                # 发送播放命令
                success = self._send_play_command(task)
                
                if success:
                    if self.wait_completion:
                        # 等待播放完成
                        self._wait_for_completion(task)
                    
                    # 调用回调
                    if task.callback:
                        try:
                            task.callback(task, success=True)
                        except Exception as e:
                            logger.error(f"[AudioPlaybackQueue] 回调函数执行失败: {e}")
                else:
                    logger.error(f"[AudioPlaybackQueue] 播放失败: {task.audio_file.name}")
                    if task.callback:
                        task.callback(task, success=False)
                
                # 清除当前任务
                self.current_task = None
                self.task_queue.task_done()
                
            except Empty:
                # 队列为空，继续等待
                continue
            except Exception as e:
                logger.error(f"[AudioPlaybackQueue] Worker异常: {e}", exc_info=True)
                time.sleep(0.5)
        
        logger.info("[AudioPlaybackQueue] Worker线程退出")
    
    def _send_play_command(self, task: AudioPlaybackTask) -> bool:
        """
        发送播放命令到audio_player
        
        Args:
            task: 播放任务
            
        Returns:
            是否成功
        """
        if not self.cmd_socket:
            logger.error("[AudioPlaybackQueue] 命令socket未初始化")
            return False
        
        try:
            # 构造命令
            command = {
                "action": "cmd.audio.play",
                "uri": str(task.audio_file.absolute()),
                "priority": task.priority,
                "ducking": task.ducking
            }
            
            # 添加元数据
            if task.metadata:
                command.update(task.metadata)
            
            # 发送命令
            logger.debug(f"[AudioPlaybackQueue] 发送播放命令: {command}")
            self.cmd_socket.send_string(json.dumps(command))
            
            # 接收响应
            try:
                reply = self.cmd_socket.recv_string()
                reply_data = json.loads(reply)
                logger.debug(f"[AudioPlaybackQueue] 播放命令响应: {reply_data}")
                
                # audio_player 返回格式: {"ok": true, "path": "...", ...}
                # 检查 ok 字段（布尔值）或 status/success 字段
                if reply_data.get('ok') is True or reply_data.get('status') == 'ok' or reply_data.get('success') is True:
                    logger.info(f"[AudioPlaybackQueue] ✅ 播放命令发送成功: {task.audio_file.name}")
                    self.is_playing = True
                    return True
                else:
                    logger.error(f"[AudioPlaybackQueue] ❌ 播放命令被拒绝: {reply_data}")
                    return False
                    
            except zmq.Again:
                logger.error("[AudioPlaybackQueue] 接收响应超时")
                return False
            
        except Exception as e:
            logger.error(f"[AudioPlaybackQueue] 发送播放命令失败: {e}", exc_info=True)
            return False
    
    def _status_loop(self):
        """状态监控线程：监听audio_player状态更新"""
        logger.info("[AudioPlaybackQueue] Status监控线程启动")
        
        while self._running:
            try:
                if not self.status_socket:
                    time.sleep(1)
                    continue
                
                # 接收状态消息
                try:
                    topic = self.status_socket.recv_string(flags=zmq.NOBLOCK)
                    data = self.status_socket.recv_string()
                    
                    status_data = json.loads(data)
                    self._handle_status_update(status_data)
                    
                except zmq.Again:
                    # 没有消息，继续等待
                    time.sleep(0.1)
                    continue
                    
            except Exception as e:
                logger.error(f"[AudioPlaybackQueue] Status监控异常: {e}", exc_info=True)
                time.sleep(0.5)
        
        logger.info("[AudioPlaybackQueue] Status监控线程退出")
    
    def _handle_status_update(self, status: Dict[str, Any]):
        """
        处理audio_player状态更新
        
        Args:
            status: 状态数据
        """
        # 检查播放状态
        state = status.get('state', '')
        
        if state == 'stopped' or state == 'finished':
            self.is_playing = False
            logger.debug(f"[AudioPlaybackQueue] 播放已停止: {status}")
        elif state == 'playing':
            self.is_playing = True
            logger.debug(f"[AudioPlaybackQueue] 正在播放: {status}")
    
    def _wait_for_completion(self, task: AudioPlaybackTask, timeout: Optional[float] = None):
        """
        等待播放完成
        
        Args:
            task: 播放任务
            timeout: 超时时间(秒)
        """
        timeout = timeout or self.timeout
        start_time = time.time()
        
        logger.info(f"[AudioPlaybackQueue] 等待播放完成: {task.audio_file.name}")
        
        while self.is_playing and (time.time() - start_time) < timeout:
            time.sleep(0.1)
        
        if self.is_playing:
            logger.warning(f"[AudioPlaybackQueue] 等待播放完成超时({timeout}s): {task.audio_file.name}")
    
    def _stop_current(self):
        """停止当前播放"""
        if not self.cmd_socket or not self.current_task:
            return
        
        try:
            command = {"action": "cmd.audio.stop"}
            self.cmd_socket.send_string(json.dumps(command))
            reply = self.cmd_socket.recv_string()
            logger.info(f"[AudioPlaybackQueue] 已停止当前播放: {reply}")
            self.is_playing = False
        except Exception as e:
            logger.error(f"[AudioPlaybackQueue] 停止播放失败: {e}")
    
    def _clear_queue(self):
        """清空队列"""
        cleared = 0
        while not self.task_queue.empty():
            try:
                self.task_queue.get_nowait()
                self.task_queue.task_done()
                cleared += 1
            except Empty:
                break
        
        if cleared > 0:
            logger.info(f"[AudioPlaybackQueue] 已清空队列，移除{cleared}个任务")
    
    def stop(self):
        """停止队列服务"""
        logger.info("[AudioPlaybackQueue] 正在停止...")
        
        self._running = False
        
        # 等待线程结束
        if self._worker_thread and self._worker_thread.is_alive():
            self._worker_thread.join(timeout=2)
        
        if self._status_thread and self._status_thread.is_alive():
            self._status_thread.join(timeout=2)
        
        # 关闭ZMQ
        if self.cmd_socket:
            self.cmd_socket.close()
        
        if self.status_socket:
            self.status_socket.close()
        
        if self.zmq_context:
            self.zmq_context.term()
        
        logger.info("[AudioPlaybackQueue] 已停止")
    
    def get_status(self) -> Dict[str, Any]:
        """
        获取队列状态
        
        Returns:
            状态字典
        """
        return {
            'enabled': self.enabled,
            'queue_size': self.task_queue.qsize(),
            'max_size': self.max_size,
            'is_playing': self.is_playing,
            'current_task': str(self.current_task.audio_file) if self.current_task else None,
            'mode': self.mode,
            'running': self._running
        }
