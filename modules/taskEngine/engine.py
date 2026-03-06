"""
TaskEngine - 任务执行引擎主类

整合意图匹配、任务调度和任务执行，提供统一的任务处理入口。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import asyncio
import logging
from typing import Dict, Any, Optional, List, Callable
from pathlib import Path

from .models import Task, TaskResult, TaskStatus, ActionType
from .intent_matcher import IntentMatcher
from .task_scheduler import TaskScheduler
from .task_executor import TaskExecutor
from .interface_registry import InterfaceRegistry, get_global_registry

logger = logging.getLogger(__name__)


class TaskEngine:
    """
    任务执行引擎
    
    整合意图匹配、任务调度和任务执行，提供：
    - 意图到任务的转换
    - 动作指令的直接执行
    - 任务生命周期管理
    - 接口统一注册
    
    使用示例:
        engine = TaskEngine()
        engine.register_interface('eye', eye_client)
        engine.register_interface('drive', drive_client)
        
        engine.start()
        
        # 处理意图
        result = await engine.process_intent('set_timer', {'duration_min': 5})
        
        # 处理动作
        result = await engine.process_action('play_animation', {'name': 'wave'})
        
        engine.stop()
    """
    
    def __init__(
        self,
        config_dir: Optional[str] = None,
        use_global_registry: bool = True
    ):
        """
        初始化任务引擎
        
        Args:
            config_dir: 配置目录路径
            use_global_registry: 是否使用全局接口注册表
        """
        self.config_dir = Path(config_dir) if config_dir else Path("/home/pi/dolydev/config")
        
        # 接口注册表
        if use_global_registry:
            self.registry = get_global_registry()
        else:
            self.registry = InterfaceRegistry()
        
        # 核心组件
        self.intent_matcher = IntentMatcher(
            str(self.config_dir / "intent_action_mapping.yaml")
        )
        self.task_executor = TaskExecutor(self.registry)
        self.task_scheduler = TaskScheduler(
            max_concurrent=3,
            executor=self._execute_task
        )
        
        # 运行状态
        self._running = False
        self._loop: Optional[asyncio.AbstractEventLoop] = None
        
        # 回调
        self._on_task_complete: List[Callable] = []
        self._on_intent_processed: List[Callable] = []
        
        logger.info("[TaskEngine] 初始化完成")
    
    def register_interface(
        self,
        name: str,
        interface: Any,
        description: str = ""
    ) -> None:
        """
        注册接口
        
        Args:
            name: 接口名称 (eye, drive, audio, widget, animation)
            interface: 接口实例
            description: 接口描述
        """
        self.registry.register(name, interface, description)
    
    def unregister_interface(self, name: str) -> bool:
        """注销接口"""
        return self.registry.unregister(name)
    
    def get_interface(self, name: str) -> Optional[Any]:
        """获取接口"""
        return self.registry.get(name)
    
    def start(self, loop: Optional[asyncio.AbstractEventLoop] = None) -> bool:
        """
        启动引擎
        
        Args:
            loop: 事件循环（可选）
            
        Returns:
            成功返回 True，否则 False
        """
        if self._running:
            logger.warning("[TaskEngine] 引擎已在运行")
            return True
        
        try:
            self._running = True
            self._loop = loop or asyncio.get_event_loop()
            
            # 启动调度器
            self.task_scheduler.start(self._loop)
            
            # 注册调度器回调
            self.task_scheduler.on_task_complete(self._on_scheduler_task_complete)
            
            logger.info("[TaskEngine] 引擎已启动")
            return True
            
        except Exception as e:
            logger.error(f"[TaskEngine] 启动失败: {e}", exc_info=True)
            self._running = False
            return False
    
    def stop(self) -> None:
        """停止引擎"""
        if not self._running:
            return
        
        self._running = False
        self.task_scheduler.stop()
        
        logger.info("[TaskEngine] 引擎已停止")
    
    async def process_intent(
        self,
        intent: str,
        entities: Optional[Dict[str, Any]] = None,
        source: str = "unknown"
    ) -> Optional[TaskResult]:
        """
        处理意图
        
        Args:
            intent: 意图名称
            entities: 实体字典
            source: 来源标识
            
        Returns:
            最后一个任务的执行结果
        """
        entities = entities or {}
        
        logger.info(f"[TaskEngine] 处理意图: {intent}, entities={entities}")
        
        # 匹配意图到任务
        tasks = self.intent_matcher.match(intent, entities)
        
        if not tasks:
            # 尝试相似度匹配
            similar_intent = self.intent_matcher.match_by_similarity(intent)
            if similar_intent:
                logger.info(f"[TaskEngine] 使用相似意图: {intent} -> {similar_intent}")
                tasks = self.intent_matcher.match(similar_intent, entities)
        
        if not tasks:
            logger.warning(f"[TaskEngine] 意图未匹配: {intent}")
            return None
        
        # 执行任务
        last_result = None
        for task in tasks:
            task.source = source
            task.metadata['intent'] = intent
            task.metadata['entities'] = entities
            
            # 调度任务
            self.task_scheduler.schedule(task)
            
            # 等待执行完成
            result = await self._wait_for_task(task.id, timeout=task.timeout_s)
            last_result = result
            
            if result and not result.success:
                logger.warning(f"[TaskEngine] 任务执行失败: {task.id}")
                break
        
        # 触发回调
        for callback in self._on_intent_processed:
            try:
                callback(intent, entities, last_result)
            except Exception as e:
                logger.error(f"[TaskEngine] on_intent_processed 回调失败: {e}")
        
        return last_result
    
    async def process_action(
        self,
        action_type: str,
        params: Dict[str, Any],
        source: str = "unknown",
        priority: int = 5
    ) -> TaskResult:
        """
        处理动作指令
        
        Args:
            action_type: 动作类型
            params: 动作参数
            source: 来源标识
            priority: 优先级
            
        Returns:
            执行结果
        """
        logger.info(f"[TaskEngine] 处理动作: {action_type}, params={params}")
        
        # 创建任务
        task = Task(
            action_type=action_type,
            params=params,
            source=source,
            priority=priority
        )
        
        # 调度任务
        self.task_scheduler.schedule(task)
        
        # 等待执行完成
        result = await self._wait_for_task(task.id, timeout=task.timeout_s)
        
        return result or TaskResult(
            task_id=task.id,
            success=False,
            status=TaskStatus.TIMEOUT,
            error="Task execution timeout"
        )
    
    async def process_text(
        self,
        text: str,
        source: str = "user_input"
    ) -> Optional[TaskResult]:
        """
        处理自然语言文本
        
        使用关键词匹配将文本转换为意图并执行
        
        Args:
            text: 用户输入文本
            source: 来源标识
            
        Returns:
            执行结果
        """
        logger.info(f"[TaskEngine] 处理文本: {text[:50]}...")
        
        # 关键词匹配
        match_result = self.intent_matcher.match_by_keywords(text)
        
        if not match_result:
            logger.warning(f"[TaskEngine] 文本未匹配到意图: {text[:30]}...")
            return None
        
        intent, entities, tasks = match_result
        
        # 执行任务
        last_result = None
        for task in tasks:
            task.source = source
            task.metadata['raw_text'] = text
            
            self.task_scheduler.schedule(task)
            result = await self._wait_for_task(task.id, timeout=task.timeout_s)
            last_result = result
            
            if result and not result.success:
                break
        
        return last_result
    
    def schedule_task(self, task: Task) -> str:
        """
        直接调度任务
        
        Args:
            task: 任务对象
            
        Returns:
            任务ID
        """
        return self.task_scheduler.schedule(task)
    
    def cancel_task(self, task_id: str) -> bool:
        """取消任务"""
        return self.task_scheduler.cancel(task_id)
    
    def get_task_status(self, task_id: str) -> Optional[TaskStatus]:
        """获取任务状态"""
        return self.task_scheduler.get_task_status(task_id)
    
    def get_task_result(self, task_id: str) -> Optional[TaskResult]:
        """获取任务结果"""
        return self.task_scheduler.get_task_result(task_id)
    
    def get_stats(self) -> Dict[str, Any]:
        """获取引擎统计信息"""
        return {
            'running': self._running,
            'scheduler': self.task_scheduler.get_stats(),
            'intents_registered': len(self.intent_matcher.list_intents()),
            'interfaces_registered': self.registry.list_interfaces()
        }
    
    def list_intents(self) -> List[str]:
        """列出所有已注册意图"""
        return self.intent_matcher.list_intents()
    
    def list_interfaces(self) -> List[str]:
        """列出所有已注册接口"""
        return self.registry.list_interfaces()
    
    async def _execute_task(self, task: Task) -> TaskResult:
        """内部任务执行方法（由调度器调用）"""
        return await self.task_executor.execute(task)
    
    async def _wait_for_task(
        self,
        task_id: str,
        timeout: float = 30.0
    ) -> Optional[TaskResult]:
        """等待任务完成"""
        start_time = asyncio.get_event_loop().time()
        
        while asyncio.get_event_loop().time() - start_time < timeout:
            result = self.task_scheduler.get_task_result(task_id)
            if result:
                return result
            
            status = self.task_scheduler.get_task_status(task_id)
            if status in (TaskStatus.FAILED, TaskStatus.CANCELLED, TaskStatus.TIMEOUT):
                return TaskResult(
                    task_id=task_id,
                    success=False,
                    status=status,
                    error=f"Task ended with status: {status.name}"
                )
            
            await asyncio.sleep(0.1)
        
        return None
    
    def _on_scheduler_task_complete(self, task: Task, result: TaskResult) -> None:
        """调度器任务完成回调"""
        for callback in self._on_task_complete:
            try:
                callback(task, result)
            except Exception as e:
                logger.error(f"[TaskEngine] on_task_complete 回调失败: {e}")
    
    def on_task_complete(self, callback: Callable) -> None:
        """注册任务完成回调"""
        self._on_task_complete.append(callback)
    
    def on_intent_processed(self, callback: Callable) -> None:
        """注册意图处理完成回调"""
        self._on_intent_processed.append(callback)
    
    def register_action_handler(
        self,
        action_type: ActionType,
        handler: Callable
    ) -> None:
        """
        注册自定义动作处理器
        
        Args:
            action_type: 动作类型
            handler: 处理器函数
        """
        self.task_executor.register_handler(action_type, handler)
    
    def reload_config(self) -> bool:
        """重新加载配置"""
        return self.intent_matcher.reload_config()


# 全局单例
_global_engine: Optional[TaskEngine] = None


def get_task_engine() -> TaskEngine:
    """获取全局任务引擎单例"""
    global _global_engine
    if _global_engine is None:
        _global_engine = TaskEngine()
    return _global_engine
