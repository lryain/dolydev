"""
任务数据模型

定义任务引擎使用的核心数据结构。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from enum import Enum, auto
from dataclasses import dataclass, field
from typing import Optional, Dict, Any, List
from datetime import datetime
import uuid


class TaskStatus(Enum):
    """任务状态"""
    PENDING = auto()      # 等待执行
    RUNNING = auto()      # 正在执行
    COMPLETED = auto()    # 执行完成
    FAILED = auto()       # 执行失败
    CANCELLED = auto()    # 已取消
    TIMEOUT = auto()      # 执行超时


class ActionType(Enum):
    """动作类型"""
    PLAY_ANIMATION = "play_animation"
    PLAY_EXPRESSION = "play_expression"
    SET_EYE_COLOR = "set_eye_color"
    MOVE_ARM = "move_arm"
    MOVE = "move"
    LED_EFFECT = "led_effect"
    SET_TIMER = "set_timer"
    PLAY_AUDIO = "play_audio"
    DISPLAY_TEXT = "display_text"
    QUERY_TIME = "query_time"
    NLU_FALLBACK = "nlu_fallback"
    CUSTOM = "custom"


class TaskPriority(Enum):
    """任务优先级"""
    LOW = 1
    NORMAL = 5
    HIGH = 8
    URGENT = 10


@dataclass
class Task:
    """
    任务定义
    
    Attributes:
        id: 任务唯一标识
        action_type: 动作类型
        params: 动作参数
        priority: 优先级
        source: 任务来源 (cloud_ai, local_nlu, user_input)
        created_at: 创建时间
        started_at: 开始执行时间
        completed_at: 完成时间
        status: 当前状态
        result: 执行结果
        timeout_s: 超时时间（秒）
        retries: 重试次数
        max_retries: 最大重试次数
        parent_id: 父任务ID（用于任务链）
        metadata: 附加元数据
    """
    action_type: ActionType
    params: Dict[str, Any]
    id: str = field(default_factory=lambda: str(uuid.uuid4())[:8])
    priority: int = 5
    source: str = "unknown"
    created_at: datetime = field(default_factory=datetime.now)
    started_at: Optional[datetime] = None
    completed_at: Optional[datetime] = None
    status: TaskStatus = TaskStatus.PENDING
    result: Optional[Dict[str, Any]] = None
    timeout_s: float = 30.0
    retries: int = 0
    max_retries: int = 3
    parent_id: Optional[str] = None
    metadata: Dict[str, Any] = field(default_factory=dict)
    
    def __post_init__(self):
        # 确保 action_type 是枚举类型
        if isinstance(self.action_type, str):
            try:
                self.action_type = ActionType(self.action_type)
            except ValueError:
                self.action_type = ActionType.CUSTOM
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            'id': self.id,
            'action_type': self.action_type.value if isinstance(self.action_type, ActionType) else str(self.action_type),
            'params': self.params,
            'priority': self.priority,
            'source': self.source,
            'created_at': self.created_at.isoformat(),
            'started_at': self.started_at.isoformat() if self.started_at else None,
            'completed_at': self.completed_at.isoformat() if self.completed_at else None,
            'status': self.status.name,
            'result': self.result,
            'timeout_s': self.timeout_s,
            'retries': self.retries,
            'metadata': self.metadata
        }
    
    @classmethod
    def from_dict(cls, data: Dict[str, Any]) -> 'Task':
        """从字典创建任务"""
        return cls(
            id=data.get('id', str(uuid.uuid4())[:8]),
            action_type=data.get('action_type', 'custom'),
            params=data.get('params', {}),
            priority=data.get('priority', 5),
            source=data.get('source', 'unknown'),
            timeout_s=data.get('timeout_s', 30.0),
            metadata=data.get('metadata', {})
        )


@dataclass
class TaskResult:
    """
    任务执行结果
    
    Attributes:
        task_id: 任务ID
        success: 是否成功
        status: 最终状态
        data: 返回数据
        error: 错误信息
        duration_ms: 执行耗时（毫秒）
        timestamp: 完成时间戳
    """
    task_id: str
    success: bool
    status: TaskStatus = TaskStatus.COMPLETED
    data: Optional[Dict[str, Any]] = None
    error: Optional[str] = None
    duration_ms: int = 0
    timestamp: datetime = field(default_factory=datetime.now)
    
    def to_dict(self) -> Dict[str, Any]:
        """转换为字典"""
        return {
            'task_id': self.task_id,
            'success': self.success,
            'status': self.status.name,
            'data': self.data,
            'error': self.error,
            'duration_ms': self.duration_ms,
            'timestamp': self.timestamp.isoformat()
        }


@dataclass
class IntentMapping:
    """
    意图到动作的映射配置
    
    Attributes:
        intent: 意图名称
        actions: 动作列表
        required_entities: 必需的实体
        optional_entities: 可选的实体
        response_template: 响应模板
        priority: 默认优先级
    """
    intent: str
    actions: List[Dict[str, Any]]
    required_entities: List[str] = field(default_factory=list)
    optional_entities: List[str] = field(default_factory=list)
    response_template: str = ""
    priority: int = 5
    
    def validate_entities(self, entities: Dict[str, Any]) -> bool:
        """验证实体是否满足要求"""
        for required in self.required_entities:
            if required not in entities:
                return False
        return True
    
    def fill_template(self, entities: Dict[str, Any]) -> str:
        """填充响应模板"""
        if not self.response_template:
            return ""
        try:
            return self.response_template.format(**entities)
        except KeyError:
            return self.response_template


@dataclass
class TaskChain:
    """
    任务链（用于复杂的多步骤任务）
    
    Attributes:
        id: 任务链ID
        tasks: 任务列表
        current_index: 当前执行索引
        status: 整体状态
        stop_on_error: 出错时是否停止
    """
    id: str = field(default_factory=lambda: str(uuid.uuid4())[:8])
    tasks: List[Task] = field(default_factory=list)
    current_index: int = 0
    status: TaskStatus = TaskStatus.PENDING
    stop_on_error: bool = True
    
    def add_task(self, task: Task) -> None:
        """添加任务"""
        task.parent_id = self.id
        self.tasks.append(task)
    
    def get_current_task(self) -> Optional[Task]:
        """获取当前任务"""
        if 0 <= self.current_index < len(self.tasks):
            return self.tasks[self.current_index]
        return None
    
    def advance(self) -> bool:
        """前进到下一个任务，返回是否还有剩余任务"""
        self.current_index += 1
        return self.current_index < len(self.tasks)
    
    def is_complete(self) -> bool:
        """检查是否全部完成"""
        return self.current_index >= len(self.tasks)
