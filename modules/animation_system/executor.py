"""
动画执行器

执行动画块序列，处理并发和顺序执行逻辑。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import asyncio
from typing import List, Optional
import logging

from .parser import AnimationBlock
from .hardware_interfaces import HardwareInterfaces
from .blocks import BlockFactory
from .blocks.base_block import BaseBlock
from .blocks.control_blocks import DelayBlock, RepeatBlock
from .blocks.led_blocks import LEDAnimationBlock, LEDAnimationColorBlock
from .blocks.sound_blocks import SoundBlock

logger = logging.getLogger(__name__)


class AnimationExecutor:
    """动画执行器"""
    
    def __init__(self, interfaces: HardwareInterfaces):
        """
        初始化执行器
        
        Args:
            interfaces: 硬件接口集合
        """
        self.interfaces = interfaces
        self._running = False
        self._paused = False
        self._current_tasks: List[asyncio.Task] = []
        self._stop_event = asyncio.Event()
        # per-target locks to enforce ordering per destination (eye/drive/audio/led/etc.)
        self._target_locks = {}
    
    async def execute(self, animation_blocks: List[AnimationBlock]) -> None:
        """
        执行动画块列表
        
        Args:
            animation_blocks: 动画块列表
        """
        if not animation_blocks:
            logger.warning("No animation blocks to execute")
            return
        
        self._running = True
        self._stop_event.clear()
        try:
            logger.info(f"Starting animation execution with {len(animation_blocks)} blocks")
            await self._execute_blocks(animation_blocks)
            # logger.info("Animation execution completed")
        except asyncio.CancelledError:
            logger.info("Animation execution cancelled")
            raise
        except Exception as e:
            logger.error(f"Error during animation execution: {e}")
            # 主动取消所有子任务，防止卡死
            self.cancel_all_tasks()
            raise
        finally:
            self._running = False
            self._current_tasks.clear()

    def cancel_all_tasks(self):
        """取消所有当前任务（可在异常或 KeyboardInterrupt 时调用）"""
        for task in self._current_tasks:
            if not task.done():
                task.cancel()
        self._stop_event.set()
    
    async def _execute_blocks(self, animation_blocks: List[AnimationBlock], is_complete_statement: bool = False) -> None:
        """
        执行动画块列表（内部方法）
        
        Args:
            animation_blocks: 动画块列表（可以是AnimationBlock或已转换的BaseBlock）
            is_complete_statement: 是否来自 complete_statement（如果是，块不需要获取目标锁）
        """
        # logger.info(f"[_execute_blocks] 开始执行块列表，共 {len(animation_blocks)} 个块 (is_complete_statement={is_complete_statement})")
        
        # 转换为 BaseBlock 实例（如果还没有转换的话）
        base_blocks = []
        for anim_block in animation_blocks:
            # 如果已经是BaseBlock，直接使用；否则通过BlockFactory转换
            if isinstance(anim_block, BaseBlock):
                # logger.debug(f"Block already converted: {anim_block.block_type}")
                base_blocks.append(anim_block)
            else:
                block = BlockFactory.create_block(anim_block)
                if block:
                    base_blocks.append(block)
            
            # 如果来自 complete_statement，标记块，使其不需要获取锁
            if is_complete_statement and base_blocks:
                setattr(base_blocks[-1], '_from_complete_statement', True)
        
        # logger.info(f"[_execute_blocks] 转换后共 {len(base_blocks)} 个 BaseBlock")
        
        if not base_blocks:
            # logger.warning("[_execute_blocks] 没有可执行的块，返回")
            return

        # --- 恢复旧的逻辑：顺序块逐个等待，并行块批量调度 ---
        # Track last target to help DelayBlock inherit the correct queue
        last_target_for_delay = ''
        i = 0
        while i < len(base_blocks):
            # respect global stop/paused signals
            if self._stop_event.is_set():
                logger.info("Stop event set, halting execution")
                break

            while self._paused:
                await asyncio.sleep(0.1)
            
            current_block = base_blocks[i]
            
            # Infer target and propagate to delay blocks
            current_target = self._get_block_target(current_block)
            if current_block.block_type == 'delay_ms':
                # DelayBlock should inherit the last non-delay target
                if not current_target:
                    current_target = last_target_for_delay
                    # Store it back so _execute_single_block can use it
                    setattr(current_block, '_inherited_target', current_target)
            else:
                # Update last target for subsequent delay blocks
                if current_target:
                    last_target_for_delay = current_target
            
            # 收集并行执行的块（从当前块开始）
            parallel_blocks = [current_block]
            j = i + 1
            
            while j < len(base_blocks) and base_blocks[j].is_parallel():
                # 并行块继承目标
                parallel_target = self._get_block_target(base_blocks[j])
                if base_blocks[j].block_type == 'delay_ms' and not parallel_target:
                    parallel_target = last_target_for_delay
                    setattr(base_blocks[j], '_inherited_target', parallel_target)
                else:
                    if parallel_target:
                        last_target_for_delay = parallel_target
                
                parallel_blocks.append(base_blocks[j])
                j += 1
            
            # logger.info(f"[_execute_blocks] 调度块 #{i}: {current_block.block_type} (并行={current_block.is_parallel()}, 目标={current_target})")
            
            # 执行块
            if len(parallel_blocks) == 1:
                # 单个块
                if current_block.is_parallel():
                    # 并行块 - 作为后台任务启动
                    # logger.debug(f"Starting parallel single block in background: {current_block.block_type}")
                    task = asyncio.create_task(
                        self._execute_single_block(current_block, animation_blocks[i])
                    )
                    self._current_tasks.append(task)
                else:
                    # 顺序块 - 同步等待
                    # logger.info(f"[_execute_blocks] 等待顺序块完成: {current_block.block_type}")
                    await self._execute_single_block(current_block, animation_blocks[i])
                    # logger.info(f"[_execute_blocks] 顺序块完成: {current_block.block_type}")
            else:
                # 多个并行块
                # logger.debug(f"Executing {len(parallel_blocks)} blocks in parallel")
                tasks = []
                for idx, block in enumerate(parallel_blocks):
                    task = asyncio.create_task(
                        self._execute_single_block(block, animation_blocks[i + idx])
                    )
                    tasks.append(task)
                
                self._current_tasks.extend(tasks)
                await asyncio.gather(*tasks, return_exceptions=True)
                
                # 清理已完成的任务
                for task in tasks:
                    if task in self._current_tasks:
                        self._current_tasks.remove(task)
            
            i = j
        
        # logger.info(f"[_execute_blocks] ✅ 块列表执行完成")
    
    async def _execute_single_block(self, block: BaseBlock, anim_block: AnimationBlock) -> None:
        """
        执行单个动画块
        
        Args:
            block: 基础块实例
            anim_block: 原始动画块（用于获取子语句）
        """
        try:
            # 检查是否来自 complete_statement（不需要获取锁）
            is_from_complete_statement = getattr(block, '_from_complete_statement', False)
            
            # determine the target for this block (including inherited target for delay blocks)
            target = getattr(block, '_inherited_target', None) or self._get_block_target(block)
            lock = None
            
            # 只有在不是来自 complete_statement 时才获取锁
            if target and not is_from_complete_statement:
                lock = self._target_locks.get(target)
                if lock is None:
                    lock = asyncio.Lock()
                    self._target_locks[target] = lock

            # logger.info(f"[_execute_single_block] 块类型: {block.block_type}, 目标: {target}, 是否并行: {block.is_parallel()}, 来自complete_statement: {is_from_complete_statement}")
            
            # run execution under lock for sequential behaviour per-target
            if lock:
                # if start_mode==0 (parallel), don't acquire the lock (allow immediate sending)
                if block.is_parallel():
                    # logger.info(f"[_execute_single_block] 执行并行块 ({block.block_type}, 目标 {target})")
                    await self._dispatch_block_execution(block)
                else:
                    # logger.info(f"[_execute_single_block] 获取锁执行顺序块 ({block.block_type}, 目标 {target})")
                    async with lock:
                        # logger.info(f"[_execute_single_block] 已获得锁，开始执行 ({block.block_type})")
                        await self._dispatch_block_execution(block)
                        # logger.info(f"[_execute_single_block] 执行完成，释放锁 ({block.block_type})")
            else:
                # unknown target or from complete_statement -> just execute
                # logger.info(f"[_execute_single_block] 无需锁的块直接执行: {block.block_type} (from_complete_statement={is_from_complete_statement})")
                # logger.info(f"[_execute_single_block] 🚀 开始执行 _dispatch_block_execution ({block.block_type})")
                await self._dispatch_block_execution(block)
                # logger.info(f"[_execute_single_block] ✅ 完成 _dispatch_block_execution ({block.block_type})")
        except Exception as e:
            # 如果是 overlay_block.py 的 ZMQ 死锁异常，主动 cancel 所有任务
            from modules.animation_system.blocks.overlay_block import OverlayBlockZMQError
            if isinstance(e, OverlayBlockZMQError):
                logger.error(f"OverlayBlockZMQError: {e}, cancel all tasks!")
                self.cancel_all_tasks()
                raise
            logger.error(f"Error executing block {block.block_type}: {e}")

    async def _dispatch_block_execution(self, block: BaseBlock) -> None:
        """
        Dispatch to the correct internal handler for block execution.
        Separated to keep lock handling simple.
        """
        if isinstance(block, RepeatBlock):
            # logger.info(f"[Dispatcher] RepeatBlock 识别，调用 _execute_repeat_block")
            await self._execute_repeat_block(block)
        elif isinstance(block, LEDAnimationBlock):
            # logger.debug(f"[Dispatcher] LEDAnimationBlock 识别")
            await self._execute_led_animation_block(block)
        elif isinstance(block, SoundBlock):
            # logger.debug(f"[Dispatcher] SoundBlock 识别")
            await self._execute_sound_block(block)
        else:
            # logger.info(f"[Dispatcher] 常规块执行: {block.block_type} (是否并行: {block.is_parallel()})")
            await block.execute(self.interfaces)
            # logger.info(f"[Dispatcher] 块执行完成: {block.block_type}")
            
            # 执行 complete_statement 块（所有块都支持）
            if block.supports_complete_statement():
                complete_blocks = block.get_complete_blocks()
                # logger.info(f"[Dispatcher] 执行 complete_statement，共 {len(complete_blocks)} 个嵌套块: {block.block_type}")
                await self._execute_blocks(complete_blocks, is_complete_statement=True)

    def _get_block_target(self, block: BaseBlock) -> str:
        """
        Infer target module string from block type or block fields.
        Examples: 'eye', 'drive:left', 'drive:right', 'audio', 'led:left', 'led:right'
        """
        bt = getattr(block, 'block_type', '')
        # repeat 块需要特殊处理 - 赋予一个虚拟 target 来确保顺序执行
        if bt == 'repeat':
            return 'control_flow'  # 为 repeat 块分配特殊的 control_flow 目标
        # delay blocks: return empty (will use inherited target)
        if bt == 'delay_ms':
            return ''
        # eye blocks
        if bt.startswith('eye'):
            return 'eye'
        # sound / audio
        if bt.startswith('sound') or bt in ('play_sound', 'sound'):
            return 'audio'
        # drive related
        if bt.startswith('drive') or bt in ('arm_set_angle', 'arm_do_action'):
            # try channel/side
            channel = getattr(block, 'channel', None) or block.get_field('channel', None)
            if not channel:
                side = block.get_field('side', None) or block.get_field('channel', None)
                channel = side
            if isinstance(channel, str):
                ch = channel.lower()
                if ch in ('left', 'l', '1'):
                    return 'drive:left'
                if ch in ('right', 'r', '2'):
                    return 'drive:right'
            return 'drive'
        # led related
        if bt.startswith('led') or 'led' in bt:
            side = block.get_field('side', None)
            if side in (1, 'left', 'LEFT'):
                return 'led:left'
            if side in (2, 'right', 'RIGHT'):
                return 'led:right'
            return 'led'
        # overlay / sprite
        if bt.startswith('overlay') or 'sprite' in bt:
            return 'overlay'
        # fallback
        return ''
    
    async def _execute_repeat_block(self, block: RepeatBlock) -> None:
        """
        执行循环块
        
        Args:
            block: 循环块
        """
        times = block.get_repeat_times()
        repeat_blocks = block.get_repeat_blocks()
        
        # logger.info(f"[RepeatBlock] ⏭️ 开始执行循环，次数={times}，包含 {len(repeat_blocks)} 个子块")
        
        for i in range(times):
            if self._stop_event.is_set():
                # logger.info(f"[RepeatBlock] ⛔ 停止事件已设置，中止循环执行")
                break
            
            # logger.info(f"[RepeatBlock] 🔄 循环迭代 {i + 1}/{times} 开始，调用 _execute_blocks")
            try:
                await self._execute_blocks(repeat_blocks)
                # logger.info(f"[RepeatBlock] ✅ 循环迭代 {i + 1}/{times} 完成")
            except Exception as e:
                logger.error(f"[RepeatBlock] ❌ 循环迭代 {i + 1}/{times} 异常: {e}", exc_info=True)
                raise
        
        # logger.info(f"[RepeatBlock] ✅ 循环执行完成")
        
        # 执行 complete_statement 块
        if block.supports_complete_statement():
            complete_blocks = block.get_complete_blocks()
            # logger.info(f"[RepeatBlock] 执行 complete_statement，共 {len(complete_blocks)} 个嵌套块")
            await self._execute_blocks(complete_blocks, is_complete_statement=True)
    
    async def _execute_led_animation_block(self, block: LEDAnimationBlock) -> None:
        """
        执行 LED 动画块
        
        Args:
            block: LED 动画块
        """
        left_blocks = block.get_led_left_blocks()
        right_blocks = block.get_led_right_blocks()
        
        # logger.debug(f"Executing LED animation: left={len(left_blocks)}, right={len(right_blocks)}")
        
        # 创建左右两个序列的任务
        tasks = []
        
        if left_blocks:
            tasks.append(asyncio.create_task(
                self._execute_led_sequence(left_blocks, side=1)
            ))
        
        if right_blocks:
            tasks.append(asyncio.create_task(
                self._execute_led_sequence(right_blocks, side=2)
            ))
        
        if tasks:
            self._current_tasks.extend(tasks)
            await asyncio.gather(*tasks, return_exceptions=True)
            
            for task in tasks:
                if task in self._current_tasks:
                    self._current_tasks.remove(task)
        
        # 执行 complete_statement 块
        if block.supports_complete_statement():
            complete_blocks = block.get_complete_blocks()
            # logger.info(f"[LEDAnimationBlock] 执行 complete_statement，共 {len(complete_blocks)} 个嵌套块")
            await self._execute_blocks(complete_blocks, is_complete_statement=True)
    
    async def _execute_led_sequence(self, led_blocks: List, side: int) -> None:
        """
        执行 LED 颜色序列
        
        Args:
            led_blocks: LED 颜色块列表（可以是AnimationBlock或BaseBlock）
            side: 灯光位置（1=左，2=右）
        """
        for led_block in led_blocks:
            if self._stop_event.is_set():
                break
            
            # 如果已经是BaseBlock，直接使用；否则通过BlockFactory转换
            if isinstance(led_block, BaseBlock):
                block = led_block
            else:
                block = BlockFactory.create_block(led_block)
            
            if block and isinstance(block, LEDAnimationColorBlock):
                block.set_side(side)
                await block.execute(self.interfaces)
    
    async def _execute_sound_block(self, block: SoundBlock) -> None:
        """
        执行声音块
        
        Args:
            block: 声音块
        """
        if not self.interfaces.sound:
            logger.warning("Sound interface not available")
            return
        
        type_id = block.get_type_id()
        name = block.get_name()
        complete_blocks = block.get_complete_blocks()
        
        logger.debug(f"Playing sound: {type_id}/{name}")
        
        wait_flag = getattr(block, 'wait', True)
        # 并行块不应阻塞其他眼睛动画，即使声明了 wait
        effective_wait = wait_flag and not block.is_parallel()
        try:
            # apply optional pre-play delay from block (delay_ms)
            delay_ms = getattr(block, 'delay_ms', 0)
            if delay_ms:
                # logger.debug(f"[Executor] delaying sound {type_id}/{name} by {delay_ms}ms")
                await asyncio.sleep(delay_ms / 1000.0)
            
            # 播放声音，根据 wait 决定是否阻塞后续眼睛/行为块
            if effective_wait:
                # logger.info(f"[Executor] Sound block blocking until completion: {type_id}/{name}")
                await self.interfaces.sound.play(type_id, name, wait=True)
            else:
                # logger.info(f"[Executor] Sound block non-blocking: {type_id}/{name}, reason="
                            # f"{'parallel start' if block.is_parallel() else 'wait=False'}")
                await self.interfaces.sound.play(type_id, name, wait=False)
            
            # 如果有完成回调块，执行它们
            if complete_blocks:
                # logger.debug(f"Executing {len(complete_blocks)} complete blocks")
                await self._execute_blocks(complete_blocks, is_complete_statement=True)
                
        except Exception as e:
            logger.error(f"Failed to execute sound block: {e}")
    
    def stop(self) -> None:
        """停止当前动画"""
        logger.info("Stopping animation execution")
        self._stop_event.set()
        
        # 取消所有运行中的任务
        for task in self._current_tasks:
            if not task.done():
                task.cancel()
        
        self._running = False
    
    def pause(self) -> None:
        """暂停动画"""
        if self._running and not self._paused:
            logger.info("Pausing animation execution")
            self._paused = True
    
    def resume(self) -> None:
        """恢复动画"""
        if self._running and self._paused:
            logger.info("Resuming animation execution")
            self._paused = False
    
    def is_running(self) -> bool:
        """检查是否正在运行"""
        return self._running
    
    def is_paused(self) -> bool:
        """检查是否已暂停"""
        return self._paused
