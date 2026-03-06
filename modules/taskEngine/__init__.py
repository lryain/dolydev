"""
TaskEngine - 任务执行引擎

负责处理来自小智的意图和动作指令，调度和执行相应的任务。

模块结构:
- __init__.py: 模块入口
- engine.py: TaskEngine 核心类
- intent_matcher.py: 意图匹配器
- task_scheduler.py: 任务调度器
- task_executor.py: 任务执行器
- interface_registry.py: 接口注册表
- action_handlers/: 各类动作处理器

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from .engine import TaskEngine
from .intent_matcher import IntentMatcher
from .task_scheduler import TaskScheduler
from .task_executor import TaskExecutor
from .interface_registry import InterfaceRegistry
from .models import Task, TaskResult, TaskStatus, ActionType

__all__ = [
    'TaskEngine',
    'IntentMatcher',
    'TaskScheduler',
    'TaskExecutor',
    'InterfaceRegistry',
    'Task',
    'TaskResult',
    'TaskStatus',
    'ActionType'
]

__version__ = '0.1.0'
