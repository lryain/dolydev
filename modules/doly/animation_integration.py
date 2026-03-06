"""
动画系统集成模块

负责将 Doly Daemon 与 animation_system 模块集成。
提供异步的动画执行接口。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import asyncio
import logging
from typing import Optional, List, Set, Dict
from pathlib import Path

logger = logging.getLogger(__name__)


class AnimationIntegration:
    """动画系统集成器"""
    
    def __init__(self, animations_path: str = "/home/pi/dolydev/assets/config/animations"):
        """
        初始化动画集成器
        
        Args:
            animations_path: 动画资源路径
        """
        self.animations_path = animations_path
        self.manager = None
        self.executor = None
        self.interfaces = None
        self._initialized = False
        
        # ★ 新增：活跃的 overlay 集合（用于 interrupt）
        # _pending_overlays: 已发送到 eyeEngine 但还未收到 started 事件的
        # _confirmed_active_overlays: 已收到 eyeEngine confirmed (started 事件) 的
        self._pending_overlays: Set[str] = set()
        self._confirmed_active_overlays: Set[str] = set()
        self._overlay_lock = asyncio.Lock()
        
    async def initialize(self) -> bool:
        """
        异步初始化动画系统
        
        Returns:
            初始化是否成功
        """
        try:
            logger.info("[AnimationIntegration] 初始化动画系统")
            
            # 初始化动画系统
            await self._init_async()
            
            logger.info("[AnimationIntegration] 初始化完成")
            self._initialized = True
            return True
            
        except Exception as e:
            logger.error(f"[AnimationIntegration] 初始化失败: {e}")
            return False
    
    async def _init_async(self):
        """异步初始化"""
        try:
            # 延迟导入以避免循环依赖
            from modules.animation_system.factory import create_real_hardware
            from modules.animation_system.animation_manager import AnimationManager
            
            # 获取当前 event loop
            loop = asyncio.get_event_loop()
            
            # 在线程中执行阻塞操作
            def init_in_thread():
                logger.info("[AnimationIntegration] 初始化硬件接口...")
                self.interfaces = create_real_hardware()
                # 将 animation_integration 引用挂载到 interfaces，便于各个 block 访问
                # 兼容两种属性名，overlay_block 会优先检测 _animation_integration
                try:
                    setattr(self.interfaces, '_animation_integration', self)
                    setattr(self.interfaces, 'animation_integration', self)
                except Exception as e:
                    logger.warning(f"[AnimationIntegration] 无法在 interfaces 挂载 self: {e}")
                logger.info("[AnimationIntegration] 硬件接口创建完成")
                
                logger.info("[AnimationIntegration] 加载动画配置...")
                self.manager = AnimationManager(self.animations_path)
                self.manager.load_animations()
                logger.info("[AnimationIntegration] 动画管理器初始化完成")
            
            # 使用 run_in_executor 在线程池中运行初始化
            await loop.run_in_executor(None, init_in_thread)
            
            # 在 event loop 中创建 executor（这样它有正确的 event loop 上下文）
            logger.info("[AnimationIntegration] 创建执行器...")
            from modules.animation_system.executor import AnimationExecutor
            self.executor = AnimationExecutor(self.interfaces)
            logger.info("[AnimationIntegration] 动画执行器创建完成")
            
        except Exception as e:
            logger.error(f"[AnimationIntegration] 初始化失败: {e}")
            import traceback
            traceback.print_exc()
            raise
    
    async def play_animation_by_file(self, file_path: str) -> bool:
        """
        按文件路径播放动画
        
        Args:
            file_path: 相对于 animations_path 的动画文件路径 (e.g., 'salsa.xml')
            
        Returns:
            执行是否成功
        """
        if not self._initialized or not self.manager or not self.executor:
            # logger.warning("[AnimationIntegration] 动画系统未初始化")
            return False
        
        try:
            # 规范化路径：若传入相对路径，则自动拼接动画目录
            full_path = Path(file_path)
            if not full_path.is_absolute():
                full_path = Path(self.animations_path) / file_path
            
            # logger.info(f"[AnimationIntegration] 播放动画文件: {full_path}")
            
            loop = asyncio.get_event_loop()
            
            # 在线程中加载动画文件
            blocks = await loop.run_in_executor(None, self.manager._load_animation_file, str(full_path))
            
            if not blocks:
                # logger.warning(f"[AnimationIntegration] 动画文件加载失败: {file_path}")
                return False
            
            # 执行动画（异步）
            await self.executor.execute(blocks)
            
            # logger.info(f"[AnimationIntegration] 动画执行完成: {file_path}")
            return True
            
        except Exception as e:
            logger.error(f"[AnimationIntegration] 动画执行失败: {e}")
            return False
    
    async def play_animation_by_category(
        self,
        category: str,
        level: int = 1,
        random_select: bool = True
    ) -> bool:
        """
        按分类播放动画
        
        Args:
            category: 动画分类 (e.g., 'ANIMATION_HAPPY')
            level: 动画等级 (默认 1)
            random_select: 是否随机选择同等级的多个动画中的一个
            
        Returns:
            执行是否成功
        """
        if not self._initialized or not self.manager or not self.executor:
            logger.warning("[AnimationIntegration] 动画系统未初始化")
            return False
        
        try:
            logger.info(f"[AnimationIntegration] 播放动画分类: {category} level={level}")
            
            # 直接在线程池中获取动画块（避免事件循环冲突）
            blocks = self.manager.get_animation(category, level, random_select)
            
            if not blocks:
                logger.warning(f"[AnimationIntegration] 动画分类不存在或获取失败: {category}")
                return False
            
            # 执行动画（异步）
            await self.executor.execute(blocks)
            
            logger.info(f"[AnimationIntegration] 动画执行完成: {category}")
            return True
            
        except Exception as e:
            logger.error(f"[AnimationIntegration] 动画执行失败: {e}")
            return False
    
    async def stop_animation(self):
        """停止当前正在播放的动画"""
        if self.executor:
            self.executor.cancel_all_tasks()
            logger.info("[AnimationIntegration] 动画已停止")
    
    async def register_overlay(self, overlay_id: str):
        """
        ★ 新增：将 overlay_id 放入待确认队列（供 overlay_block 调用）
        
        Args:
            overlay_id: 从 play_sequence_animations 返回的 overlay_id
        """
        async with self._overlay_lock:
            self._pending_overlays.add(overlay_id)
            logger.debug(f"[AnimationIntegration] overlay 进入待确认: {overlay_id}, 待确认数: {len(self._pending_overlays)}")
    
    async def register_confirmed_overlay(self, overlay_id: str):
        """
        ★ 新增：当收到 eyeEngine 的 overlay.started 事件时调用
        只有收到确认的 overlay 才被视为真正活跃
        
        Args:
            overlay_id: 从 eyeEngine overlay.started 事件中获取
        """
        async with self._overlay_lock:
            # 从待确认转移到已确认
            self._pending_overlays.discard(overlay_id)
            self._confirmed_active_overlays.add(overlay_id)
            logger.info(f"[SeqTracker] 确认激活: {overlay_id}, 活跃数: {len(self._confirmed_active_overlays)}")
    
    async def unregister_overlay(self, overlay_id: str):
        """
        ★ 新增：取消注册 overlay_id（当收到 completed/stopped/failed 事件时调用）
        
        Args:
            overlay_id: overlay_id
        """
        async with self._overlay_lock:
            self._confirmed_active_overlays.discard(overlay_id)
            self._pending_overlays.discard(overlay_id)
            logger.info(f"[SeqTracker] 取消激活: {overlay_id}, 剩余活跃数: {len(self._confirmed_active_overlays)}")
    
    async def interrupt_all_async(self):
        """
        ★ 改进版本：中断所有已确认的活跃 seq 动画
        异步方法，用于高优先级命令（如 wakeup）
        
        只中断已收到 eyeEngine confirmed 的 overlay（即真正在播放的）
        """
        async with self._overlay_lock:
            overlays_to_stop = list(self._confirmed_active_overlays)
        
        logger.info(f"[中断] 准备停止 {len(overlays_to_stop)} 个 seq 动画: {overlays_to_stop}")
        
        try:
            # 停止所有已确认的活跃 overlay
            for overlay_id in overlays_to_stop:
                try:
                    logger.info(f"[中断] 发送停止命令: {overlay_id}")
                    if self.interfaces and hasattr(self.interfaces, 'eye'):
                        await self.interfaces.eye.stop_overlay_sequence(overlay_id)
                    await self.unregister_overlay(overlay_id)
                except Exception as e:
                    logger.error(f"[中断] 停止失败: {overlay_id} - {e}")
            
            # 取消所有 executor 任务
            if self.executor:
                self.executor.cancel_all_tasks()
                logger.info("[中断] 已取消 executor 任务")
            
            logger.info("[中断] 完成")
            
        except Exception as e:
            logger.error(f"[中断] 中断失败: {e}")
    
    def interrupt_all_sync(self):
        """
        ★ 同步版本：供daemon同步调用（不使用 await）
        创建任务并在后台运行
        """
        logger.info("[AnimationIntegration] 中断所有动画（同步版本）")
        try:
            # 尝试获取当前 event loop，如果不存在则创建新的任务
            try:
                loop = asyncio.get_event_loop()
                if loop.is_running():
                    # 如果 event loop 在运行，使用 create_task
                    asyncio.create_task(self.interrupt_all_async())
                    logger.debug("[AnimationIntegration] 已提交异步中断任务")
                    return
                else:
                    # 事件循环存在但未运行（少见），直接运行协程
                    loop.run_until_complete(self.interrupt_all_async())
                    return
            except RuntimeError:
                # 无事件循环，直接执行协程
                asyncio.run(self.interrupt_all_async())
                return

            # 降级方案：直接取消 executor 任务
            if self.executor:
                self.executor.cancel_all_tasks()
                logger.info("[AnimationIntegration] 已取消 executor 任务（降级方案）")
                
        except Exception as e:
            logger.error(f"[AnimationIntegration] 中断失败: {e}")
    
    def get_categories(self) -> dict:
        """
        获取所有动画分类
        
        Returns:
            分类字典
        """
        if not self.manager:
            return {}
        return self.manager.categories


# 全局单例
_animation_integration: Optional[AnimationIntegration] = None


async def get_animation_integration() -> AnimationIntegration:
    """
    获取动画集成实例（单例模式）
    
    Returns:
        AnimationIntegration 实例
    """
    global _animation_integration
    
    if _animation_integration is None:
        _animation_integration = AnimationIntegration()
        if not await _animation_integration.initialize():
            logger.error("[AnimationIntegration] 全局初始化失败")
    
    return _animation_integration
