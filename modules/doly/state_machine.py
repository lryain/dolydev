"""
Doly 状态机

管理 Doly 的运行状态和模式切换。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import time
import logging
from enum import Enum
from typing import Optional, Callable, Dict, Any
from dataclasses import dataclass

logger = logging.getLogger(__name__)


class DolyState(Enum):
    """Doly 运行状态"""
    
    IDLE = "idle"              # 待机状态 - 等待唤醒
    ACTIVATED = "activated"    # 激活状态 - 语音交互中
    EXPLORING = "exploring"    # 探索状态 - 自主巡航
    SLEEPING = "sleeping"      # 休眠状态 - 低功耗
    CHARGING = "charging"      # 充电状态
    BLOCKLY = "blockly"        # Blockly 执行状态
    GESTURE_INTERACTION = "gesture_interaction"  # 手势互动状态 - TOF跟踪手部距离
    FACE_RECOGNITION = "face_recognition"        # 人脸识别状态


class DolyMode(Enum):
    """Doly 运行模式"""
    
    AUTONOMOUS = "autonomous"  # 自主模式
    BLOCKLY = "blockly"        # Blockly 编程模式
    MANUAL = "manual"          # 手动控制模式


@dataclass
class StateConfig:
    """状态配置"""
    
    name: str
    timeout: float = 0.0           # 超时时间（秒），0 表示无超时
    next_state_on_timeout: Optional[DolyState] = None  # 超时后的下一个状态
    allow_interrupt: bool = True   # 是否允许被打断
    entry_animation: str = ""      # 进入状态时的动画
    exit_animation: str = ""       # 退出状态时的动画


# 状态变化回调类型
StateChangeCallback = Callable[[DolyState, DolyState], None]


class DolyStateMachine:
    """
    Doly 状态机
    
    管理：
    - 状态切换
    - 超时处理
    - 模式切换
    - 状态进入/退出回调
    """
    
    # 默认状态配置
    DEFAULT_STATE_CONFIGS: Dict[DolyState, StateConfig] = {
        DolyState.IDLE: StateConfig(
            name="idle",
            timeout=300.0,  # 测试用：改为 5 分钟，以便观察 snooze
            next_state_on_timeout=DolyState.EXPLORING,
            allow_interrupt=True,
            entry_animation=""
        ),
        DolyState.ACTIVATED: StateConfig(
            name="activated",
            timeout=30.0,  # 30秒无操作回到待机
            next_state_on_timeout=DolyState.IDLE,
            allow_interrupt=True,
            entry_animation="wakeup.xml"
        ),
        DolyState.EXPLORING: StateConfig(
            name="exploring",
            timeout=120.0,  # 2分钟后回到待机
            next_state_on_timeout=DolyState.IDLE,
            allow_interrupt=True,
            entry_animation=""
        ),
        DolyState.SLEEPING: StateConfig(
            name="sleeping",
            timeout=0.0,  # 无超时
            next_state_on_timeout=None,
            allow_interrupt=True,
            entry_animation="sleep.xml"
        ),
        DolyState.FACE_RECOGNITION: StateConfig(
            name="face_recognition",
            timeout=45.0,
            next_state_on_timeout=DolyState.IDLE,
            allow_interrupt=True,
            entry_animation=""
        ),
        DolyState.CHARGING: StateConfig(
            name="charging",
            timeout=0.0,
            next_state_on_timeout=None,
            allow_interrupt=False,
            entry_animation=""
        ),
        DolyState.BLOCKLY: StateConfig(
            name="blockly",
            timeout=0.0,  # Blockly 模式无超时
            next_state_on_timeout=None,
            allow_interrupt=False,  # Blockly 执行中不可打断
            entry_animation=""
        ),
    }
    
    def __init__(self, 
                 initial_state: DolyState = DolyState.IDLE,
                 initial_mode: DolyMode = DolyMode.AUTONOMOUS):
        """
        初始化状态机
        
        Args:
            initial_state: 初始状态
            initial_mode: 初始模式
        """
        self._current_state = initial_state
        self._previous_state = initial_state
        self._current_mode = initial_mode
        self._previous_mode = initial_mode
        
        self._state_entry_time = time.time()
        self._state_configs = self.DEFAULT_STATE_CONFIGS.copy()
        
        # 回调
        self._on_state_change: Optional[StateChangeCallback] = None
        self._on_mode_change: Optional[Callable[[DolyMode, DolyMode], None]] = None
        
        # 是否暂停超时检查
        self._timeout_paused = False
        
        logger.info(f"[StateMachine] 初始化完成 state={initial_state.value} mode={initial_mode.value}")
    
    @property
    def current_state(self) -> DolyState:
        """当前状态"""
        return self._current_state
    
    @property
    def previous_state(self) -> DolyState:
        """上一个状态"""
        return self._previous_state
    
    @property
    def current_mode(self) -> DolyMode:
        """当前模式"""
        return self._current_mode
    
    @property
    def state_duration(self) -> float:
        """当前状态持续时间（秒）"""
        return time.time() - self._state_entry_time
    
    def set_state_change_callback(self, callback: StateChangeCallback) -> None:
        """设置状态变化回调"""
        self._on_state_change = callback
    
    def set_mode_change_callback(self, callback: Callable[[DolyMode, DolyMode], None]) -> None:
        """设置模式变化回调"""
        self._on_mode_change = callback
    
    def update_state_config(self, state: DolyState, config: StateConfig) -> None:
        """更新状态配置"""
        self._state_configs[state] = config
        logger.debug(f"[StateMachine] 更新状态配置: {state.value}")
    
    def transition_to(self, new_state: DolyState, force: bool = False) -> bool:
        """
        切换到新状态
        
        Args:
            new_state: 新状态
            force: 是否强制切换（忽略 allow_interrupt）
            
        Returns:
            是否切换成功
        """
        if new_state == self._current_state:
            logger.debug(f"[StateMachine] 已在状态 {new_state.value}，忽略")
            return True
        
        # 检查是否允许打断
        current_config = self._state_configs.get(self._current_state)
        if current_config and not current_config.allow_interrupt and not force:
            logger.warning(f"[StateMachine] 状态 {self._current_state.value} 不允许打断")
            return False
        
        # 执行切换
        old_state = self._current_state
        self._previous_state = old_state
        self._current_state = new_state
        self._state_entry_time = time.time()
        
        logger.info(f"[StateMachine] 状态切换: {old_state.value} -> {new_state.value}")
        
        # 触发回调
        if self._on_state_change:
            try:
                self._on_state_change(old_state, new_state)
            except Exception as e:
                logger.error(f"[StateMachine] 状态变化回调异常: {e}")
        
        return True
    
    def switch_mode(self, new_mode: DolyMode) -> bool:
        """
        切换运行模式
        
        Args:
            new_mode: 新模式
            
        Returns:
            是否切换成功
        """
        if new_mode == self._current_mode:
            return True
        
        old_mode = self._current_mode
        self._previous_mode = old_mode
        self._current_mode = new_mode
        
        logger.info(f"[StateMachine] 模式切换: {old_mode.value} -> {new_mode.value}")
        
        # 模式切换时的状态处理
        if new_mode == DolyMode.BLOCKLY:
            # 切换到 Blockly 模式，进入 BLOCKLY 状态
            self.transition_to(DolyState.BLOCKLY, force=True)
        elif new_mode == DolyMode.AUTONOMOUS and old_mode == DolyMode.BLOCKLY:
            # 从 Blockly 切换回自主模式，回到 IDLE
            self.transition_to(DolyState.IDLE, force=True)
        
        # 触发回调
        if self._on_mode_change:
            try:
                self._on_mode_change(old_mode, new_mode)
            except Exception as e:
                logger.error(f"[StateMachine] 模式变化回调异常: {e}")
        
        return True
    
    def pause_timeout(self) -> None:
        """暂停超时检查"""
        self._timeout_paused = True
        logger.debug("[StateMachine] 暂停超时检查")
    
    def resume_timeout(self) -> None:
        """恢复超时检查"""
        self._timeout_paused = False
        self._state_entry_time = time.time()  # 重置计时
        logger.debug("[StateMachine] 恢复超时检查")
    
    def reset_timeout(self) -> None:
        """重置当前状态的超时计时"""
        self._state_entry_time = time.time()
        logger.debug("[StateMachine] 重置超时计时")
    
    def check_timeout(self) -> Optional[DolyState]:
        """
        检查是否超时
        
        Returns:
            如果超时，返回下一个状态；否则返回 None
        """
        if self._timeout_paused:
            return None
        
        config = self._state_configs.get(self._current_state)
        if not config or config.timeout <= 0:
            return None
        
        if self.state_duration >= config.timeout:
            logger.info(f"[StateMachine] 状态 {self._current_state.value} 超时 ({config.timeout}s)")
            return config.next_state_on_timeout
        
        return None
    
    def update(self) -> None:
        """
        更新状态机（检查超时并自动切换）
        
        应在主循环中定期调用
        """
        next_state = self.check_timeout()
        if next_state:
            self.transition_to(next_state)
    
    def get_state_info(self) -> Dict[str, Any]:
        """获取当前状态信息"""
        config = self._state_configs.get(self._current_state, StateConfig(name="unknown"))
        return {
            'current_state': self._current_state.value,
            'previous_state': self._previous_state.value,
            'current_mode': self._current_mode.value,
            'state_duration': self.state_duration,
            'timeout': config.timeout,
            'allow_interrupt': config.allow_interrupt,
        }
    
    def is_blockly_mode(self) -> bool:
        """是否在 Blockly 模式"""
        return self._current_mode == DolyMode.BLOCKLY
    
    def is_autonomous_mode(self) -> bool:
        """是否在自主模式"""
        return self._current_mode == DolyMode.AUTONOMOUS


if __name__ == '__main__':
    # 简单测试
    logging.basicConfig(level=logging.DEBUG, 
                        format='[%(asctime)s] [%(levelname)s] %(message)s')
    
    sm = DolyStateMachine()
    
    # 设置回调
    def on_state_change(old: DolyState, new: DolyState):
        print(f"状态变化: {old.value} -> {new.value}")
    
    sm.set_state_change_callback(on_state_change)
    
    # 测试状态切换
    print(f"当前状态: {sm.current_state.value}")
    sm.transition_to(DolyState.ACTIVATED)
    sm.transition_to(DolyState.IDLE)
    
    # 测试模式切换
    sm.switch_mode(DolyMode.BLOCKLY)
    print(f"当前状态: {sm.current_state.value}")
    print(f"当前模式: {sm.current_mode.value}")
