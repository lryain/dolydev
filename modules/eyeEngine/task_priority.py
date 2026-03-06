"""
任务优先级管理器

管理 EyeEngine 的任务优先级，确保高优先级任务可以打断低优先级任务

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
import threading
import time
from typing import Optional, Callable, Any, Dict, List
from dataclasses import dataclass
from enum import IntEnum

logger = logging.getLogger(__name__)


class TaskPriority(IntEnum):
    """任务优先级枚举"""
    CRITICAL = 10    # 关键任务（如系统警告）
    HIGH = 8         # 高优先级（如用户交互）
    NORMAL = 5       # 普通优先级（默认）
    LOW = 3          # 低优先级（如自动表情）
    IDLE = 1         # 空闲任务（如待机动画）


@dataclass
class Task:
    """任务描述"""
    task_id: str
    priority: int
    callback: Callable
    args: tuple = ()
    kwargs: dict = None
    blocking: bool = True
    timeout: Optional[float] = None
    started_at: Optional[float] = None
    metadata: Dict[str, Any] = None
    
    def __post_init__(self):
        if self.kwargs is None:
            self.kwargs = {}
        if self.metadata is None:
            self.metadata = {}


class TaskPriorityManager:
    """
    任务优先级管理器
    
    功能：
    - 管理任务队列和优先级
    - 自动打断低优先级任务
    - 支持任务超时
    - 线程安全
    """
    
    def __init__(self, max_priority: int = 10, min_priority: int = 1, default_priority: int = 5, enabled: bool = True, max_duration: Optional[float] = None):
        """
        初始化优先级管理器
        
        Args:
            max_priority: 最高优先级
            min_priority: 最低优先级
            default_priority: 默认优先级
            enabled: 是否启用优先级检查（禁用时新任务总是打断旧任务）
        """
        self.max_priority = max_priority
        self.min_priority = min_priority
        self.default_priority = default_priority
        self.enabled = enabled
        self.max_duration = max_duration
        
        # 当前任务
        self._current_task: Optional[Task] = None
        # 任务队列：相同优先级的任务排队等待
        self._task_queue: List[Task] = []
        # 日志辅助：记录每次状态变化
        logger.info(f"[TaskPriorityManager INIT] max={max_priority} min={min_priority} default={default_priority} enabled={enabled}")
        self._task_thread: Optional[threading.Thread] = None
        self._pending_thread_join: Optional[threading.Thread] = None  # 待 join 的旧线程
        self._stop_event = threading.Event()
        
        # 线程安全
        self._lock = threading.Lock()
        
        # 任务完成回调
        self._on_task_complete: Optional[Callable[[str], None]] = None
        self._on_task_interrupted: Optional[Callable[[str, str], None]] = None
        # 任务失败回调 (task_id, traceback_str)
        self._on_task_failed: Optional[Callable[[str, str], None]] = None

        # 诊断：排队/最近事件
        self._pending_queue: List[Dict[str, Any]] = []
        self._last_events: Dict[str, Optional[Dict[str, Any]]] = {
            "complete": None,
            "interrupted": None,
            "failed": None,
            "rejected": None,
        }
    
    def set_callbacks(self, 
                     on_complete: Optional[Callable[[str], None]] = None,
                     on_interrupted: Optional[Callable[[str, str], None]] = None,
                     on_failed: Optional[Callable[[str, str], None]] = None):
        """
        设置任务回调
        
        Args:
            on_complete: 任务完成回调 (task_id)
            on_interrupted: 任务被打断回调 (old_task_id, new_task_id)
            on_failed: 任务失败回调 (task_id, traceback_str)
        """
        self._on_task_complete = on_complete
        self._on_task_interrupted = on_interrupted
        self._on_task_failed = on_failed
    
    def submit_task(self, 
                   task_id: str,
                   callback: Callable,
                   priority: Optional[int] = None,
                   args: tuple = (),
                   kwargs: Optional[dict] = None,
                   blocking: bool = True,
                   timeout: Optional[float] = None,
                   metadata: Optional[Dict[str, Any]] = None) -> bool:
        """
        提交任务
        
        Args:
            task_id: 任务 ID
            callback: 任务回调函数
            priority: 任务优先级（None 使用默认）
            args: 回调函数位置参数
            kwargs: 回调函数关键字参数
            blocking: 是否阻塞等待任务完成
            timeout: 任务超时（秒）
        
        Returns:
            True 如果任务已接受，False 如果被拒绝（优先级不足）
        """
        if priority is None:
            priority = self.default_priority
        
        # 限制优先级范围
        priority = max(self.min_priority, min(self.max_priority, priority))
        
        task = Task(
            task_id=task_id,
            priority=priority,
            callback=callback,
            args=args,
            kwargs=kwargs or {},
            blocking=blocking,
            timeout=timeout,
            metadata=metadata or {}
        )
        
        execute_async = not blocking
        pending_thread_to_join = None
        
        with self._lock:
            # 检查是否需要打断当前任务
            if self._current_task is not None:
                # 如果启用了优先级检查且优先级【严格低于】当前，则拒绝
                if self.enabled and priority < self._current_task.priority:
                    self._record_reject(task, reason="priority_insufficient")
                    holder = self._describe_task(self._current_task)
                    logger.info(f"[PRIORITY] action=submit task={task_id} pri={priority} result=denied reason=priority_insufficient holder={holder}")
                    return False
                
                # 如果优先级【等于】当前任务，加入队列等待
                if self.enabled and priority == self._current_task.priority:
                    self._task_queue.append(task)
                    logger.info(f"[PRIORITY] action=enqueue task={task_id} pri={priority} queue_size={len(self._task_queue)} current={self._current_task.task_id}")
                    return True
                
                # 打断当前任务（优先级更高）
                logger.info(f"[PRIORITY] action=preempt task={task_id} pri={priority} victim={self._current_task.task_id} victim_pri={self._current_task.priority}")
                
                old_task = self._current_task
                old_task_id = old_task.task_id
                self._stop_current_task()
                pending_thread_to_join = self._pending_thread_join  # 获取待 join 的线程
                
                # 触发打断回调
                if self._on_task_interrupted:
                    try:
                        self._on_task_interrupted(old_task_id, task_id)
                    except Exception as e:
                        logger.error(f"任务打断回调异常: {e}")
                self._last_events["interrupted"] = self._describe_task(old_task)
            
            # 启动新任务
            self._current_task = task
            self._stop_event.clear()
            logger.info(f"[PRIORITY] action=submit task={task_id} pri={priority} result=granted current={self._describe_task(task)}")

            task.started_at = time.time()

        # 锁外 join 旧线程（如果有），避免死锁
        if pending_thread_to_join:
            self._join_pending_thread_obj(pending_thread_to_join)

        # 锁外执行，避免阻塞其他请求
        if blocking:
            self._run_task(task)
        else:
            self._task_thread = threading.Thread(target=self._run_task, args=(task,), daemon=True)
            self._task_thread.start()
        
        return True
    
    def _join_pending_thread_obj(self, thread: Optional[threading.Thread]):
        """在锁外 join 待处理的线程对象"""
        if thread and thread.is_alive():
            logger.debug(f"等待旧线程结束...")
            thread.join(timeout=0.2)  # 短超时

    def submit_task_sync(self,
                         task_id: str,
                         callback: Callable,
                         priority: Optional[int] = None,
                         args: tuple = (),
                         kwargs: Optional[dict] = None,
                         timeout: Optional[float] = None,
                         metadata: Optional[Dict[str, Any]] = None):
        """
        同步提交任务并返回 callback 的返回值。如果任务被拒绝，返回 (False, None).

        Returns:
            (accepted: bool, result): accepted 表示是否被接受，result 为 callback 返回值或异常信息字符串（当执行抛出异常时）。
        """
        if priority is None:
            priority = self.default_priority

        priority = max(self.min_priority, min(self.max_priority, priority))

        if kwargs is None:
            kwargs = {}

        with self._lock:
            # 检查当前任务
            if self._current_task is not None:
                if self.enabled and priority < self._current_task.priority:
                    logger.info(f"任务 {task_id} (优先级 {priority}) 被拒绝，当前任务 {self._current_task.task_id} 优先级更高 ({self._current_task.priority})")
                    return (False, None)

                # 打断当前任务
                logger.info(f"任务 {task_id} (优先级 {priority}) 打断任务 {self._current_task.task_id} (优先级 {self._current_task.priority})")
                old_task_id = self._current_task.task_id
                self._stop_current_task()
                if self._on_task_interrupted:
                    try:
                        self._on_task_interrupted(old_task_id, task_id)
                    except Exception as e:
                        logger.error(f"任务打断回调异常: {e}")

            # 设置为当前任务的占位（不需要在 Task 对象中存放回调结果）
            task = Task(task_id=task_id, priority=priority, callback=callback, args=args, kwargs=kwargs, blocking=True, timeout=timeout, metadata=metadata or {})
            self._current_task = task
            self._stop_event.clear()
            task.started_at = time.time()

        # 在锁外执行回调（保证不会死锁），但仍把当前任务设置好以阻止其他任务
        task_failed = False
        try:
            result = None
            try:
                result = callback(*args, **(kwargs or {}))
            except Exception:
                import traceback
                tb = traceback.format_exc()
                task_failed = True
                logger.error(f"任务执行异常 {task_id}: {tb}")
                if self._on_task_failed:
                    try:
                        self._on_task_failed(task_id, tb)
                    except Exception as cb_e:
                        logger.error(f"任务失败回调异常: {cb_e}")
                # 清理当前任务并返回异常字符串
                with self._lock:
                    if self._current_task and self._current_task.task_id == task_id:
                        self._current_task = None
                        self._last_events["failed"] = self._describe_task(task)
                return (True, tb)

            # 正常完成，触发完成回调
            if self._on_task_complete:
                try:
                    self._on_task_complete(task_id)
                except Exception as e:
                    logger.error(f"任务完成回调异常: {e}")

            return (True, result)

        finally:
            with self._lock:
                if self._current_task and self._current_task.task_id == task.task_id:
                    self._current_task = None
                    key = "failed" if task_failed else "complete"
                    self._last_events[key] = self._describe_task(task)
    
    def _run_task(self, task: Task):
        """执行任务"""
        finished_ok = False
        try:
            logger.debug(f"开始执行任务: {task.task_id} (优先级 {task.priority})")
            task.callback(*task.args, **task.kwargs)
            logger.debug(f"任务完成: {task.task_id}")
            finished_ok = True
            
            # 触发完成回调
            if self._on_task_complete:
                try:
                    self._on_task_complete(task.task_id)
                except Exception as e:
                    logger.error(f"任务完成回调异常: {e}")
        
        except Exception as e:
            # 捕获完整 traceback，并触发 on_failed 回调
            import traceback
            tb = traceback.format_exc()
            logger.error(f"任务执行异常 {task.task_id}: {e}\n{tb}")
            if self._on_task_failed:
                try:
                    self._on_task_failed(task.task_id, tb)
                except Exception as cb_e:
                    logger.error(f"任务失败回调异常: {cb_e}")
            with self._lock:
                self._last_events["failed"] = self._describe_task(task)
        
        finally:
            # 任务结束后，检查队列中是否有待执行的任务
            next_task = None
            with self._lock:
                if self._current_task and self._current_task.task_id == task.task_id:
                    self._current_task = None
                    if finished_ok:
                        self._last_events["complete"] = self._describe_task(task)
                
                # 从队列中取出下一个任务
                if self._task_queue:
                    next_task = self._task_queue.pop(0)
                    self._current_task = next_task
                    self._stop_event.clear()
                    next_task.started_at = time.time()
                    logger.info(f"[PRIORITY] action=dequeue task={next_task.task_id} pri={next_task.priority} queue_remaining={len(self._task_queue)}")
            
            # 在锁外执行下一个任务
            if next_task:
                if next_task.blocking:
                    self._run_task(next_task)
                else:
                    self._task_thread = threading.Thread(target=self._run_task, args=(next_task,), daemon=True)
                    self._task_thread.start()
    
    def _stop_current_task(self):
        """停止当前任务（必须在锁内调用）"""
        if self._current_task is None:
            return
        
        # 设置停止事件
        self._stop_event.set()
        
        # 不要在锁内等待线程，这会导致死锁
        # 只记录需要停止的线程，由主程序稍后处理
        self._pending_thread_join = self._task_thread if (self._task_thread and self._task_thread.is_alive()) else None
        
        self._current_task = None
        self._task_thread = None
    
    def _join_pending_thread(self):
        """在锁外 join 待处理的线程"""
        if self._pending_thread_join and self._pending_thread_join.is_alive():
            logger.debug(f"等待旧线程结束...")
            self._pending_thread_join.join(timeout=0.2)  # 减少超时时间
            self._pending_thread_join = None
    
    def stop_current_task(self) -> bool:
        """
        停止当前任务
        
        Returns:
            True 如果有任务被停止
        """
        with self._lock:
            if self._current_task is None:
                return False
            
            logger.info(f"[PRIORITY] action=stop task={self._current_task.task_id} pri={self._current_task.priority}")
            self._stop_current_task()
            return True
    
    def get_current_task_id(self) -> Optional[str]:
        """获取当前任务 ID"""
        with self._lock:
            return self._current_task.task_id if self._current_task else None
    
    def get_current_priority(self) -> Optional[int]:
        """获取当前任务优先级"""
        with self._lock:
            return self._current_task.priority if self._current_task else None
    
    def is_running(self) -> bool:
        """是否有任务正在运行"""
        with self._lock:
            return self._current_task is not None
    
    def should_stop(self) -> bool:
        """当前任务是否应该停止（供任务内部检查）"""
        return self._stop_event.is_set()

    def set_enabled(self, enabled: bool):
        """开启/关闭优先级检查，同时记录快照"""
        self.enabled = bool(enabled)
        logger.info(f"[PRIORITY] action=set_enabled enabled={self.enabled}")

    def get_snapshot(self) -> Dict[str, Any]:
        """获取当前优先级管理器状态快照"""
        with self._lock:
            now = time.time()
            current = self._describe_task(self._current_task, now)
            pending = [self._with_age(p, now) for p in list(self._pending_queue)]
            return {
                "timestamp": now,
                "enabled": self.enabled,
                "max_priority": self.max_priority,
                "min_priority": self.min_priority,
                "default_priority": self.default_priority,
                "max_duration": self.max_duration,
                "current": current,
                "pending": pending,
                "last_events": self._last_events.copy()
            }

    # internal helpers
    def _record_reject(self, task: Task, reason: str):
        entry = self._describe_task(task)
        entry.update({"reason": reason, "timestamp": time.time()})
        self._pending_queue.append(entry)
        if len(self._pending_queue) > 20:
            self._pending_queue.pop(0)
        self._last_events["rejected"] = entry

    def _describe_task(self, task: Optional[Task], now: Optional[float] = None) -> Optional[Dict[str, Any]]:
        if task is None:
            return None
        if now is None:
            now = time.time()
        started = task.started_at or now
        return {
            "task_id": task.task_id,
            "priority": task.priority,
            "started_at": started,
            "elapsed_ms": int((now - started) * 1000),
            "metadata": task.metadata
        }

    def _with_age(self, entry: Dict[str, Any], now: float) -> Dict[str, Any]:
        data = dict(entry)
        ts = entry.get("timestamp", now)
        data["age_ms"] = int((now - ts) * 1000)
        return data
