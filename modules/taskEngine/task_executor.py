"""
任务执行器

负责实际执行各类任务，调用相应的接口完成动作。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import asyncio
import logging
import time
from typing import Dict, Any, Optional, Callable
from datetime import datetime

from .models import Task, TaskResult, TaskStatus, ActionType
from .interface_registry import InterfaceRegistry

logger = logging.getLogger(__name__)


class TaskExecutor:
    """
    任务执行器
    
    功能:
    - 执行各类动作任务
    - 动作处理器注册
    - 超时控制
    - 重试机制
    
    使用示例:
        executor = TaskExecutor(registry)
        result = await executor.execute(task)
    """
    
    def __init__(self, registry: InterfaceRegistry):
        """
        初始化执行器
        
        Args:
            registry: 接口注册表
        """
        self.registry = registry
        self._action_handlers: Dict[str, Callable] = {}
        
        # 注册默认处理器
        self._register_default_handlers()
        
        logger.info("[TaskExecutor] 初始化完成")
    
    def _register_default_handlers(self) -> None:
        """注册默认动作处理器"""
        self.register_handler(ActionType.PLAY_ANIMATION, self._handle_play_animation)
        self.register_handler(ActionType.PLAY_EXPRESSION, self._handle_play_expression)
        self.register_handler(ActionType.SET_EYE_COLOR, self._handle_set_eye_color)
        self.register_handler(ActionType.MOVE_ARM, self._handle_move_arm)
        self.register_handler(ActionType.MOVE, self._handle_move)
        self.register_handler(ActionType.LED_EFFECT, self._handle_led_effect)
        self.register_handler(ActionType.SET_TIMER, self._handle_set_timer)
        self.register_handler(ActionType.PLAY_AUDIO, self._handle_play_audio)
        self.register_handler(ActionType.DISPLAY_TEXT, self._handle_display_text)
        self.register_handler(ActionType.QUERY_TIME, self._handle_query_time)
    
    def register_handler(
        self, 
        action_type: ActionType, 
        handler: Callable
    ) -> None:
        """
        注册动作处理器
        
        Args:
            action_type: 动作类型
            handler: 处理器函数 (async def handler(task, registry) -> TaskResult)
        """
        key = action_type.value if isinstance(action_type, ActionType) else str(action_type)
        self._action_handlers[key] = handler
        logger.debug(f"[TaskExecutor] 注册处理器: {key}")
    
    def unregister_handler(self, action_type: ActionType) -> None:
        """注销动作处理器"""
        key = action_type.value if isinstance(action_type, ActionType) else str(action_type)
        self._action_handlers.pop(key, None)
    
    async def execute(self, task: Task) -> TaskResult:
        """
        执行任务
        
        Args:
            task: 任务对象
            
        Returns:
            执行结果
        """
        start_time = time.time()
        
        # 获取动作类型
        action_key = (
            task.action_type.value 
            if isinstance(task.action_type, ActionType) 
            else str(task.action_type)
        )
        
        logger.info(f"[TaskExecutor] 执行任务: {task.id} (action={action_key})")
        
        # 获取处理器
        handler = self._action_handlers.get(action_key)
        
        if handler is None:
            # 尝试使用自定义处理器
            handler = self._action_handlers.get(ActionType.CUSTOM.value)
        
        if handler is None:
            logger.warning(f"[TaskExecutor] 未找到处理器: {action_key}")
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=f"No handler for action type: {action_key}"
            )
        
        # 执行（带超时控制）
        try:
            result = await asyncio.wait_for(
                self._execute_with_retry(handler, task),
                timeout=task.timeout_s
            )
            result.duration_ms = int((time.time() - start_time) * 1000)
            return result
            
        except asyncio.TimeoutError:
            logger.error(f"[TaskExecutor] 任务超时: {task.id}")
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.TIMEOUT,
                error=f"Task execution timeout ({task.timeout_s}s)",
                duration_ms=int((time.time() - start_time) * 1000)
            )
        except Exception as e:
            logger.error(f"[TaskExecutor] 任务执行失败: {task.id}, 错误: {e}")
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=str(e),
                duration_ms=int((time.time() - start_time) * 1000)
            )
    
    async def _execute_with_retry(
        self, 
        handler: Callable, 
        task: Task
    ) -> TaskResult:
        """带重试的执行"""
        last_error = None
        
        for attempt in range(task.max_retries + 1):
            try:
                if asyncio.iscoroutinefunction(handler):
                    result = await handler(task, self.registry)
                else:
                    result = handler(task, self.registry)
                
                if result.success:
                    return result
                
                last_error = result.error
                
            except Exception as e:
                last_error = str(e)
                logger.warning(f"[TaskExecutor] 任务执行失败 (attempt {attempt + 1}): {e}")
            
            if attempt < task.max_retries:
                task.retries += 1
                await asyncio.sleep(0.5 * (attempt + 1))  # 退避
        
        return TaskResult(
            task_id=task.id,
            success=False,
            status=TaskStatus.FAILED,
            error=f"Max retries exceeded. Last error: {last_error}"
        )
    
    # ==================== 默认动作处理器 ====================
    
    async def _handle_play_animation(
        self, 
        task: Task, 
        registry: InterfaceRegistry
    ) -> TaskResult:
        """处理播放动画"""
        import asyncio
        params = task.params
        name = params.get('name', '')
        category = params.get('category', '')
        level = params.get('level', 1)
        priority = params.get('priority', 5)
        loop = params.get('loop', False)
        
        try:
            # 尝试通过 animation 接口播放
            animation = registry.get('animation')
            if animation:
                if category:
                    method = getattr(animation, 'play_category', None)
                    if method:
                        if asyncio.iscoroutinefunction(method):
                            result = await method(category, level, priority)
                        else:
                            result = method(category, level, priority)
                else:
                    method = getattr(animation, 'play_animation', None)
                    if method:
                        if asyncio.iscoroutinefunction(method):
                            result = await method(name, priority=priority)
                        else:
                            result = method(name, priority=priority)
                
                return TaskResult(
                    task_id=task.id,
                    success=True,
                    status=TaskStatus.COMPLETED,
                    data={'animation': name or category, 'result': result if 'result' in dir() else None}
                )
            
            # 回退到眼睛接口
            eye = registry.get('eye')
            if eye:
                method = getattr(eye, 'play_animation', None)
                if method:
                    if asyncio.iscoroutinefunction(method):
                        await method(
                            category=category or 'ANIMATION',
                            animation=name,
                            priority=priority
                        )
                    else:
                        method(
                            category=category or 'ANIMATION',
                            animation=name,
                            priority=priority
                        )
                return TaskResult(
                    task_id=task.id,
                    success=True,
                    status=TaskStatus.COMPLETED,
                    data={'animation': name}
                )
            
            # 没有接口时也返回成功（模拟模式）
            logger.info(f"[TaskExecutor] 模拟动画: {name or category}")
            return TaskResult(
                task_id=task.id,
                success=True,
                status=TaskStatus.COMPLETED,
                data={'animation': name or category, 'simulated': True}
            )
            
        except Exception as e:
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=str(e)
            )
    
    async def _handle_play_expression(
        self, 
        task: Task, 
        registry: InterfaceRegistry
    ) -> TaskResult:
        """处理播放表情"""
        params = task.params
        category = params.get('category', 'EXPRESSION')
        name = params.get('name', 'neutral')
        priority = params.get('priority', 6)
        
        try:
            eye = registry.get('eye')
            if eye:
                # 尝试调用 play_animation 方法
                method = getattr(eye, 'play_animation', None)
                if method:
                    import asyncio
                    if asyncio.iscoroutinefunction(method):
                        await method(
                            category=category,
                            animation=name,
                            priority=priority
                        )
                    else:
                        method(
                            category=category,
                            animation=name,
                            priority=priority
                        )
                
                return TaskResult(
                    task_id=task.id,
                    success=True,
                    status=TaskStatus.COMPLETED,
                    data={'expression': name}
                )
            
            # 没有接口时也返回成功（模拟模式）
            logger.info(f"[TaskExecutor] 模拟表情: {name}")
            return TaskResult(
                task_id=task.id,
                success=True,
                status=TaskStatus.COMPLETED,
                data={'expression': name, 'simulated': True}
            )
            
        except Exception as e:
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=str(e)
            )
    
    async def _handle_set_eye_color(
        self, 
        task: Task, 
        registry: InterfaceRegistry
    ) -> TaskResult:
        """处理设置眼睛颜色"""
        params = task.params
        color = params.get('color', 'blue')
        side = params.get('side', 'BOTH')
        
        try:
            eye = registry.get('eye')
            if eye:
                eye.set_iris_color(color, side)
                return TaskResult(
                    task_id=task.id,
                    success=True,
                    status=TaskStatus.COMPLETED,
                    data={'color': color}
                )
            
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error="Eye interface not available"
            )
            
        except Exception as e:
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=str(e)
            )
    
    async def _handle_move_arm(
        self, 
        task: Task, 
        registry: InterfaceRegistry
    ) -> TaskResult:
        """处理手臂动作"""
        params = task.params
        side = params.get('side', 'left')
        position = params.get('position', 'up')
        duration_ms = params.get('duration_ms', 500)
        
        try:
            drive = registry.get('drive')
            if drive:
                # 根据 side 和 position 计算舵机角度
                channel = 0 if side == 'left' else 1
                angle = 90 if position == 'up' else 0
                
                drive.set_servo(channel, angle)
                await asyncio.sleep(duration_ms / 1000.0)
                
                return TaskResult(
                    task_id=task.id,
                    success=True,
                    status=TaskStatus.COMPLETED,
                    data={'side': side, 'position': position}
                )
            
            # 模拟执行
            logger.info(f"[TaskExecutor] 模拟手臂动作: {side} {position}")
            await asyncio.sleep(0.5)
            return TaskResult(
                task_id=task.id,
                success=True,
                status=TaskStatus.COMPLETED,
                data={'side': side, 'position': position, 'simulated': True}
            )
            
        except Exception as e:
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=str(e)
            )
    
    async def _handle_move(
        self, 
        task: Task, 
        registry: InterfaceRegistry
    ) -> TaskResult:
        """处理移动"""
        params = task.params
        direction = params.get('direction', 'forward')
        speed = params.get('speed', 50)
        duration_ms = params.get('duration_ms', 1000)
        
        try:
            drive = registry.get('drive')
            if drive:
                # 根据方向设置电机速度
                if direction == 'forward':
                    drive.set_motor(speed, speed)
                elif direction == 'backward':
                    drive.set_motor(-speed, -speed)
                elif direction == 'left':
                    drive.set_motor(-speed, speed)
                elif direction == 'right':
                    drive.set_motor(speed, -speed)
                
                await asyncio.sleep(duration_ms / 1000.0)
                drive.set_motor(0, 0)  # 停止
                
                return TaskResult(
                    task_id=task.id,
                    success=True,
                    status=TaskStatus.COMPLETED,
                    data={'direction': direction, 'duration_ms': duration_ms}
                )
            
            # 模拟执行
            logger.info(f"[TaskExecutor] 模拟移动: {direction} {duration_ms}ms")
            await asyncio.sleep(0.5)
            return TaskResult(
                task_id=task.id,
                success=True,
                status=TaskStatus.COMPLETED,
                data={'direction': direction, 'simulated': True}
            )
            
        except Exception as e:
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=str(e)
            )
    
    async def _handle_led_effect(
        self, 
        task: Task, 
        registry: InterfaceRegistry
    ) -> TaskResult:
        """处理LED效果"""
        params = task.params
        effect = params.get('effect', 'breathe')
        color = params.get('color', '#FFFFFF')
        speed = params.get('speed', 50)
        side = params.get('side', 'both')
        
        try:
            drive = registry.get('drive')
            if drive:
                drive.set_led(effect=effect, color=color, speed=speed, side=side)
                return TaskResult(
                    task_id=task.id,
                    success=True,
                    status=TaskStatus.COMPLETED,
                    data={'effect': effect, 'color': color}
                )
            
            # 模拟执行
            logger.info(f"[TaskExecutor] 模拟LED: {effect} {color}")
            return TaskResult(
                task_id=task.id,
                success=True,
                status=TaskStatus.COMPLETED,
                data={'effect': effect, 'simulated': True}
            )
            
        except Exception as e:
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=str(e)
            )
    
    async def _handle_set_timer(
        self, 
        task: Task, 
        registry: InterfaceRegistry
    ) -> TaskResult:
        """处理设置定时器"""
        params = task.params
        duration_min = params.get('duration_min', 1)
        message = params.get('message', '时间到了')
        sound = params.get('sound', 'alarm')
        
        try:
            widget = registry.get('widget')
            if widget:
                result = widget.set_timer(duration_min * 60, message)
                return TaskResult(
                    task_id=task.id,
                    success=True,
                    status=TaskStatus.COMPLETED,
                    data={'duration_min': duration_min, 'message': message, 'timer_id': result}
                )
            
            # 模拟执行
            logger.info(f"[TaskExecutor] 模拟定时器: {duration_min}分钟 - {message}")
            return TaskResult(
                task_id=task.id,
                success=True,
                status=TaskStatus.COMPLETED,
                data={'duration_min': duration_min, 'simulated': True}
            )
            
        except Exception as e:
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=str(e)
            )
    
    async def _handle_play_audio(
        self, 
        task: Task, 
        registry: InterfaceRegistry
    ) -> TaskResult:
        """处理播放音频"""
        params = task.params
        audio_type = params.get('type', 'sfx')
        name = params.get('name', '')
        volume = params.get('volume', 80)
        
        try:
            audio = registry.get('audio')
            if audio:
                audio.play(name, audio_type=audio_type, volume=volume)
                return TaskResult(
                    task_id=task.id,
                    success=True,
                    status=TaskStatus.COMPLETED,
                    data={'type': audio_type, 'name': name}
                )
            
            # 模拟执行
            logger.info(f"[TaskExecutor] 模拟音频: {audio_type}/{name}")
            return TaskResult(
                task_id=task.id,
                success=True,
                status=TaskStatus.COMPLETED,
                data={'type': audio_type, 'simulated': True}
            )
            
        except Exception as e:
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=str(e)
            )
    
    async def _handle_display_text(
        self, 
        task: Task, 
        registry: InterfaceRegistry
    ) -> TaskResult:
        """处理显示文本"""
        params = task.params
        text = params.get('text', '')
        duration_s = params.get('duration_s', 5)
        position = params.get('position', 'center')
        font_size = params.get('font_size', 24)
        
        try:
            eye = registry.get('eye')
            if eye and hasattr(eye, 'display_text'):
                eye.display_text(
                    text=text,
                    duration_s=duration_s,
                    position=position,
                    font_size=font_size
                )
                return TaskResult(
                    task_id=task.id,
                    success=True,
                    status=TaskStatus.COMPLETED,
                    data={'text': text}
                )
            
            # 功能未实现，返回待实现状态
            logger.info(f"[TaskExecutor] 显示文本（待实现）: {text}")
            return TaskResult(
                task_id=task.id,
                success=True,
                status=TaskStatus.COMPLETED,
                data={'text': text, 'note': 'display_text not yet implemented'}
            )
            
        except Exception as e:
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=str(e)
            )
    
    async def _handle_query_time(
        self, 
        task: Task, 
        registry: InterfaceRegistry
    ) -> TaskResult:
        """处理查询时间"""
        try:
            now = datetime.now()
            time_str = now.strftime('%H:%M')
            
            # 中文时间描述
            hour = now.hour
            minute = now.minute
            
            if hour < 6:
                period = '凌晨'
            elif hour < 12:
                period = '上午'
            elif hour < 14:
                period = '中午'
            elif hour < 18:
                period = '下午'
            else:
                period = '晚上'
            
            hour_12 = hour if hour <= 12 else hour - 12
            time_desc = f"{period}{hour_12}点{minute}分"
            
            return TaskResult(
                task_id=task.id,
                success=True,
                status=TaskStatus.COMPLETED,
                data={
                    'current_time': time_str,
                    'time_description': time_desc,
                    'hour': hour,
                    'minute': minute
                }
            )
            
        except Exception as e:
            return TaskResult(
                task_id=task.id,
                success=False,
                status=TaskStatus.FAILED,
                error=str(e)
            )
