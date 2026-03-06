"""
任务调度器

负责任务的排队、优先级管理和调度执行。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import asyncio
import heapq
import logging
import threading
import time
from typing import Dict, List, Optional, Callable
from datetime import datetime
from queue import PriorityQueue
from dataclasses import dataclass, field

from .models import Task, TaskResult, TaskStatus, TaskChain

logger = logging.getLogger(__name__)


@dataclass(order=True)
class PrioritizedTask:
    """优先级任务包装"""
    priority: int
    created_at: float = field(compare=True)
    task: Task = field(compare=False)
    
    def __init__(self, task: Task):
        self.priority = -task.priority  # 负数实现大优先级先执行
        self.created_at = task.created_at.timestamp()
        self.task = task


class TaskScheduler:
    """
    任务调度器
    
    功能:
    - 任务优先级队列管理
    - 任务生命周期管理
    - 并发控制
    - 定时任务支持
    - 任务链调度
    
    使用示例:
        scheduler = TaskScheduler()
        scheduler.start()
        
        task_id = scheduler.schedule(task)
        scheduler.cancel(task_id)
        
        scheduler.stop()
    """
    
    def __init__(
        self,
        max_concurrent: int = 3,
        executor: Optional[Callable] = None
    ):
        """
        初始化调度器
        
        Args:
            max_concurrent: 最大并发任务数
            executor: 任务执行器（可选，用于实际执行任务）
        """
        self.max_concurrent = max_concurrent
        self.executor = executor
        
        # 任务队列
        self._pending_queue: List[PrioritizedTask] = []  # 待执行任务（堆）
        self._running_tasks: Dict[str, Task] = {}        # 执行中任务
        self._completed_tasks: Dict[str, TaskResult] = {}  # 已完成任务
        self._task_chains: Dict[str, TaskChain] = {}     # 任务链
        
        # 定时任务
        self._scheduled_tasks: Dict[str, asyncio.TimerHandle] = {}
        
        # 并发控制
        self._semaphore: Optional[asyncio.Semaphore] = None
        self._lock = threading.Lock()
        
        # 运行状态
        self._running = False
        self._scheduler_task: Optional[asyncio.Task] = None
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        
        # 回调
        self._on_task_complete: List[Callable] = []
        self._on_task_start: List[Callable] = []
        
        logger.info(f"[TaskScheduler] 初始化完成，最大并发: {max_concurrent}")
    
    def schedule(self, task: Task) -> str:
        """
        调度任务
        
        Args:
            task: 任务对象
            
        Returns:
            任务ID
        """
        with self._lock:
            heapq.heappush(self._pending_queue, PrioritizedTask(task))
            logger.info(f"[TaskScheduler] 任务入队: {task.id} (priority={task.priority}, type={task.action_type})")
        
        return task.id
    
    def schedule_delayed(self, task: Task, delay_s: float) -> str:
        """
        延迟调度任务
        
        Args:
            task: 任务对象
            delay_s: 延迟秒数
            
        Returns:
            任务ID
        """
        if self._loop is None:
            logger.warning("[TaskScheduler] 调度器未启动，无法延迟调度")
            return self.schedule(task)  # 回退到立即调度
        
        def _delayed_schedule():
            self.schedule(task)
        
        handle = self._loop.call_later(delay_s, _delayed_schedule)
        self._scheduled_tasks[task.id] = handle
        
        logger.info(f"[TaskScheduler] 任务延迟调度: {task.id} (delay={delay_s}s)")
        return task.id
    
    def schedule_at(self, task: Task, run_at: datetime) -> str:
        """
        定时调度任务
        
        Args:
            task: 任务对象
            run_at: 执行时间
            
        Returns:
            任务ID
        """
        delay_s = (run_at - datetime.now()).total_seconds()
        if delay_s < 0:
            delay_s = 0
        
        return self.schedule_delayed(task, delay_s)
    
    def schedule_chain(self, chain: TaskChain) -> str:
        """
        调度任务链
        
        Args:
            chain: 任务链
            
        Returns:
            任务链ID
        """
        if not chain.tasks:
            logger.warning("[TaskScheduler] 空任务链，跳过调度")
            return chain.id
        
        self._task_chains[chain.id] = chain
        
        # 调度第一个任务
        first_task = chain.get_current_task()
        if first_task:
            self.schedule(first_task)
        
        logger.info(f"[TaskScheduler] 任务链入队: {chain.id} (tasks={len(chain.tasks)})")
        return chain.id
    
    def cancel(self, task_id: str) -> bool:
        """
        取消任务
        
        Args:
            task_id: 任务ID
            
        Returns:
            是否取消成功
        """
        with self._lock:
            # 检查是否在待执行队列中
            for i, pt in enumerate(self._pending_queue):
                if pt.task.id == task_id:
                    pt.task.status = TaskStatus.CANCELLED
                    self._pending_queue.pop(i)
                    heapq.heapify(self._pending_queue)
                    logger.info(f"[TaskScheduler] 任务已取消: {task_id}")
                    return True
            
            # 检查定时任务
            if task_id in self._scheduled_tasks:
                self._scheduled_tasks[task_id].cancel()
                del self._scheduled_tasks[task_id]
                logger.info(f"[TaskScheduler] 定时任务已取消: {task_id}")
                return True
            
            # 检查是否正在执行
            if task_id in self._running_tasks:
                self._running_tasks[task_id].status = TaskStatus.CANCELLED
                # 注意：实际中断执行需要执行器支持
                logger.warning(f"[TaskScheduler] 任务正在执行，标记为取消: {task_id}")
                return True
        
        logger.warning(f"[TaskScheduler] 任务不存在: {task_id}")
        return False
    
    def cancel_chain(self, chain_id: str) -> bool:
        """取消任务链"""
        if chain_id not in self._task_chains:
            return False
        
        chain = self._task_chains[chain_id]
        chain.status = TaskStatus.CANCELLED
        
        # 取消所有未完成的任务
        for task in chain.tasks:
            if task.status == TaskStatus.PENDING:
                self.cancel(task.id)
        
        logger.info(f"[TaskScheduler] 任务链已取消: {chain_id}")
        return True
    
    def get_task_status(self, task_id: str) -> Optional[TaskStatus]:
        """获取任务状态"""
        with self._lock:
            # 检查待执行队列
            for pt in self._pending_queue:
                if pt.task.id == task_id:
                    return pt.task.status
            
            # 检查执行中
            if task_id in self._running_tasks:
                return self._running_tasks[task_id].status
            
            # 检查已完成
            if task_id in self._completed_tasks:
                return self._completed_tasks[task_id].status
        
        return None
    
    def get_task_result(self, task_id: str) -> Optional[TaskResult]:
        """获取任务结果"""
        return self._completed_tasks.get(task_id)
    
    def get_pending_count(self) -> int:
        """获取待执行任务数量"""
        return len(self._pending_queue)
    
    def get_running_count(self) -> int:
        """获取执行中任务数量"""
        return len(self._running_tasks)
    
    def get_stats(self) -> Dict:
        """获取调度器统计信息"""
        return {
            'pending': len(self._pending_queue),
            'running': len(self._running_tasks),
            'completed': len(self._completed_tasks),
            'scheduled': len(self._scheduled_tasks),
            'chains': len(self._task_chains),
            'is_running': self._running
        }
    
    async def _dequeue_task(self) -> Optional[Task]:
        """从队列取出一个任务"""
        with self._lock:
            if self._pending_queue:
                pt = heapq.heappop(self._pending_queue)
                return pt.task
        return None
    
    async def _mark_running(self, task: Task) -> None:
        """标记任务为运行中"""
        task.status = TaskStatus.RUNNING
        task.started_at = datetime.now()
        self._running_tasks[task.id] = task
        
        for callback in self._on_task_start:
            try:
                callback(task)
            except Exception as e:
                logger.error(f"[TaskScheduler] on_task_start 回调失败: {e}")
    
    async def _mark_completed(self, task: Task, result: TaskResult) -> None:
        """标记任务完成"""
        task.status = result.status
        task.completed_at = datetime.now()
        task.result = result.data
        
        self._running_tasks.pop(task.id, None)
        self._completed_tasks[task.id] = result
        
        # 处理任务链
        if task.parent_id and task.parent_id in self._task_chains:
            chain = self._task_chains[task.parent_id]
            if result.success or not chain.stop_on_error:
                if chain.advance():
                    next_task = chain.get_current_task()
                    if next_task:
                        self.schedule(next_task)
                else:
                    chain.status = TaskStatus.COMPLETED
            else:
                chain.status = TaskStatus.FAILED
        
        for callback in self._on_task_complete:
            try:
                callback(task, result)
            except Exception as e:
                logger.error(f"[TaskScheduler] on_task_complete 回调失败: {e}")
    
    async def _scheduler_loop(self) -> None:
        """调度器主循环"""
        self._semaphore = asyncio.Semaphore(self.max_concurrent)
        
        while self._running:
            try:
                # 尝试获取任务
                task = await self._dequeue_task()
                
                if task is None:
                    await asyncio.sleep(0.1)  # 队列为空，等待
                    continue
                
                # 并发控制
                await self._semaphore.acquire()
                
                # 启动任务执行
                asyncio.create_task(self._execute_task(task))
                
            except asyncio.CancelledError:
                break
            except Exception as e:
                logger.error(f"[TaskScheduler] 调度器循环错误: {e}")
                await asyncio.sleep(0.5)
    
    async def _execute_task(self, task: Task) -> None:
        """执行单个任务"""
        try:
            await self._mark_running(task)
            logger.debug(f"[TaskScheduler] 开始执行任务: {task.id}")
            
            start_time = time.time()
            
            if self.executor:
                # 使用外部执行器
                result = await self.executor(task)
            else:
                # 默认：模拟执行
                await asyncio.sleep(0.1)
                result = TaskResult(
                    task_id=task.id,
                    success=True,
                    status=TaskStatus.COMPLETED,
                    data={'simulated': True}
                )
            
            result.duration_ms = int((time.time() - start_time) * 1000)
            await self._mark_completed(task, result)
            
            logger.debug(f"[TaskScheduler] 任务完成: {task.id} (duration={result.duration_ms}ms)")
            
        except asyncio.TimeoutError:
            result = TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.TIMEOUT,
                error="Task execution timeout"
            )
            await self._mark_completed(task, result)
            logger.warning(f"[TaskScheduler] 任务超时: {task.id}")
            
        except Exception as e:
            result = TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=str(e)
            )
            await self._mark_completed(task, result)
            logger.error(f"[TaskScheduler] 任务执行失败: {task.id}, 错误: {e}")
            
        finally:
            if self._semaphore:
                self._semaphore.release()
    
    def start(self, loop: Optional[asyncio.AbstractEventLoop] = None) -> None:
        """启动调度器"""
        if self._running:
            logger.warning("[TaskScheduler] 调度器已在运行")
            return
        
        self._running = True
        self._loop = loop or asyncio.get_event_loop()
        self._scheduler_task = self._loop.create_task(self._scheduler_loop())
        
        logger.info("[TaskScheduler] 调度器已启动")
    
    def stop(self) -> None:
        """停止调度器"""
        if not self._running:
            return
        
        self._running = False
        
        if self._scheduler_task:
            self._scheduler_task.cancel()
            self._scheduler_task = None
        
        # 取消所有定时任务
        for handle in self._scheduled_tasks.values():
            handle.cancel()
        self._scheduled_tasks.clear()
        
        logger.info("[TaskScheduler] 调度器已停止")
    
    def on_task_complete(self, callback: Callable) -> None:
        """注册任务完成回调"""
        self._on_task_complete.append(callback)
    
    def on_task_start(self, callback: Callable) -> None:
        """注册任务开始回调"""
        self._on_task_start.append(callback)
    
    def set_executor(self, executor: Callable) -> None:
        """设置任务执行器"""
        self.executor = executor
        logger.debug("[TaskScheduler] 已设置任务执行器")
