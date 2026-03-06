"""
动画管理器

统一管理动画播放，支持优先级和中断机制

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import time
import logging
import threading
from typing import Optional, Dict, Any, Callable
from pathlib import Path

logger = logging.getLogger(__name__)


class AnimationManager:
    """动画播放管理器"""
    
    def __init__(self):
        """初始化动画管理器"""
        # 动画集成实例(由外部设置)
        self.animation_integration = None
        
        # eyeEngine 客户端(由外部设置)
        self.eye_client = None
        
        # 当前播放的动画
        self.current_animation: Optional[str] = None
        self.current_priority: int = 0
        
        # 动画完成回调
        self.on_animation_complete: Optional[Callable] = None
        
        logger.info("AnimationManager 初始化完成")
    
    def set_animation_integration(self, integration):
        """
        设置动画集成实例
        
        Args:
            integration: AnimationIntegration 实例
        """
        self.animation_integration = integration
        logger.info("已设置动画集成")
    
    def set_eye_client(self, eye_client):
        """
        设置 eyeEngine 客户端
        
        Args:
            eye_client: EyeEngineClient 实例
        """
        self.eye_client = eye_client
        logger.info("已设置 eyeEngine 客户端")
    
    def play_animation(self, animation: str, priority: int = 5, 
                      hold_duration: float = 0.0, **kwargs) -> bool:
        """
        播放动画(通用接口)
        
        Args:
            animation: 动画名称或文件名
            priority: 优先级(0-10, 越高越优先)
            hold_duration: 保持时长(秒)
            **kwargs: 其他参数
            
        Returns:
            是否成功提交
        """
        # ★★★ 优先级检查：只有当前正在播放动画时才检查 ★★★
        # 简化逻辑：允许相同或更高优先级的动画播放
        if self.current_animation and priority < self.current_priority:
            logger.debug(f"[Animation] 优先级不足，跳过: {animation} (p={priority} < {self.current_priority})")
            return False
        
        # 判断动画类型
        if animation.endswith('.xml'):
            # 行为动画
            result = self.play_behavior(animation, priority)
            # ★★★ 播放成功后，重置优先级避免阻塞后续命令 ★★★
            if result:
                # 使用线程延迟重置优先级（假设动画播放至少需要1秒）
                import threading
                def reset_priority():
                    import time
                    time.sleep(1.0)
                    self.current_priority = 0
                    logger.debug(f"[Animation] 优先级已重置")
                threading.Thread(target=reset_priority, daemon=True).start()
            return result
        elif '.' in animation:
            # 带分类的眼睛动画 (如 "HAPPY.happy_1")
            parts = animation.split('.')
            if len(parts) == 2:
                category, anim_name = parts
                return self.play_eye_animation(category, anim_name, priority, hold_duration)
            else:
                logger.warning(f"[Animation] 无效的动画格式: {animation}")
                return False
        else:
            # 只有分类名，随机播放该分类的动画
            return self.play_eye_animation(animation, None, priority, hold_duration)
    
    def play_behavior(self, animation_file: str, priority: int = 5) -> bool:
        """
        播放行为动画
        
        Args:
            animation_file: 动画文件名(如 happy_1.xml)
            priority: 优先级
            
        Returns:
            是否成功提交
        """
        if not self.animation_integration:
            logger.warning("[Animation] 动画集成未初始化，无法播放行为动画")
            return False
        
        try:
            # ★★★ 修复：使用正确的方法名 play_animation_by_file ★★★
            import asyncio
            
            # 获取或创建事件循环
            try:
                loop = asyncio.get_event_loop()
                if loop.is_closed():
                    loop = asyncio.new_event_loop()
                    asyncio.set_event_loop(loop)
            except RuntimeError:
                loop = asyncio.new_event_loop()
                asyncio.set_event_loop(loop)
            
            # 执行异步播放任务
            coro = self.animation_integration.play_animation_by_file(animation_file)
            
            # 如果当前没有运行的事件循环，则阻塞执行
            if loop.is_running():
                # 如果已有事件循环在运行，创建任务但不等待
                task = asyncio.create_task(coro)
                logger.info(f"🎬 [Animation] 提交行为动画任务: {animation_file}, priority={priority}")
            else:
                # 如果没有事件循环运行，使用 run_until_complete 阻塞执行
                result = loop.run_until_complete(coro)
                if result:
                    logger.info(f"🎬 [Animation] 播放行为动画: {animation_file}, priority={priority}")
                else:
                    logger.warning(f"[Animation] 播放行为动画返回失败: {animation_file}")
            
            self.current_animation = animation_file
            self.current_priority = priority
            return True
            
        except Exception as e:
            logger.error(f"[Animation] 播放行为动画失败: {animation_file}, error={e}")
            import traceback
            traceback.print_exc()
            return False
    
    def play_eye_animation(self, category: str, animation: Optional[str] = None,
                          priority: int = 5, hold_duration: float = 0.0) -> bool:
        """
        播放眼睛动画
        
        Args:
            category: 动画分类(如 HAPPY, SAD, BLINK)
            animation: 动画名称(可选，None则随机播放该分类)
            priority: 优先级
            hold_duration: 保持时长(秒)
            
        Returns:
            是否成功提交
        """
        if not self.eye_client:
            logger.warning("[Animation] eyeEngine 客户端未初始化，无法播放眼睛动画")
            return False
        
        try:
            result = self.eye_client.play_animation(
                category=category,
                animation=animation,
                priority=priority,
                hold_duration=hold_duration
            )
            
            if result:
                anim_name = f"{category}.{animation}" if animation else category
                self.current_animation = anim_name
                self.current_priority = priority
                logger.info(f"👁️ [Animation] 播放眼睛动画: {anim_name}, priority={priority}")
            
            return result
        except Exception as e:
            logger.error(f"[Animation] 播放眼睛动画失败: {category}, error={e}")
            return False

    def play_category(self, category: str, level: int = 1, priority: int = 5) -> bool:
        """
        按分类播放行为动画
        
        优先尝试通过 animation_integration 播放，如果不可用则回退到 eye_client
        
        Args:
            category: 动画分类名称 (如 ANIMATION_HAPPY, ANIMATION_ANGER)
            level: 动画级别 (默认 1)
            priority: 优先级 (默认 5)
            
        Returns:
            是否成功提交
        """
        # 优先级检查
        if self.current_animation and priority < self.current_priority:
            logger.debug(f"[Animation] 优先级不足，跳过分类动画: {category}")
            return False
        
        # 优先使用 animation_integration
        if self.animation_integration:
            try:
                import asyncio
                
                # 获取或创建事件循环
                try:
                    loop = asyncio.get_event_loop()
                    if loop.is_closed():
                        loop = asyncio.new_event_loop()
                        asyncio.set_event_loop(loop)
                except RuntimeError:
                    loop = asyncio.new_event_loop()
                    asyncio.set_event_loop(loop)
                
                # 使用分类播放
                coro = self.animation_integration.play_animation_by_category(
                    category=category,
                    level=level,
                    random_select=True
                )
                
                if loop.is_running():
                    asyncio.create_task(coro)
                    logger.info(f"🎬 [Animation] 提交分类动画任务: {category} level={level}")
                else:
                    result = loop.run_until_complete(coro)
                    if result:
                        logger.info(f"🎬 [Animation] 播放分类动画: {category} level={level}")
                    else:
                        logger.warning(f"[Animation] 分类动画播放返回失败: {category}")
                
                self.current_animation = f"{category}.level{level}"
                self.current_priority = priority
                
                # 延迟重置优先级
                def reset_priority():
                    time.sleep(1.0)
                    self.current_priority = 0
                threading.Thread(target=reset_priority, daemon=True).start()
                
                return True
                
            except Exception as e:
                logger.warning(f"[Animation] 通过 animation_integration 播放失败: {e}")
                # 回退到 eye_client
        
        # 回退: 使用 eye_client 播放
        if self.eye_client:
            try:
                # 尝试使用 play_behavior 命令
                result = self.eye_client.play_behavior(
                    behavior=category,
                    level=level,
                    priority=priority
                )
                
                if result:
                    self.current_animation = f"{category}.level{level}"
                    self.current_priority = priority
                    logger.info(f"👁️ [Animation] 通过eyeClient播放分类动画: {category} level={level}")
                    return True
            except Exception as e:
                logger.warning(f"[Animation] 通过 eye_client 播放失败: {e}")
        
        logger.error(f"[Animation] 无法播放分类动画: {category}，没有可用的播放器")
        return False
    
    def blink(self, animation: Optional[str] = None, priority: int = 5) -> bool:
        """
        眨眼
        
        Args:
            animation: 眨眼动画名称(可选)
            priority: 优先级
            
        Returns:
            是否成功
        """
        if not self.eye_client:
            logger.warning("[Animation] eyeEngine 客户端未初始化")
            return False
        
        try:
            return self.eye_client.blink(animation=animation, priority=priority)
        except Exception as e:
            logger.error(f"[Animation] 眨眼失败: {e}")
            return False
    
    def stop(self) -> bool:
        """停止当前动画"""
        if self.eye_client:
            try:
                self.eye_client.stop()
            except Exception as e:
                logger.error(f"[Animation] 停止眼睛动画失败: {e}")
        
        if self.animation_integration:
            try:
                # TODO: 实现动画集成的停止方法
                pass
            except Exception as e:
                logger.error(f"[Animation] 停止行为动画失败: {e}")
        
        self.current_animation = None
        self.current_priority = 0
        logger.info("[Animation] 已停止动画")
        return True
    
    def interrupt_all(self, reason: str = "interrupt") -> bool:
        """
        中断所有动画(用于紧急情况，如悬崖检测)
        
        Args:
            reason: 中断原因
            
        Returns:
            是否成功
        """
        logger.warning(f"⚠️ [Animation] 中断所有动画: {reason}")
        
        # 停止眼睛动画
        if self.eye_client:
            try:
                self.eye_client.stop()
            except Exception as e:
                logger.error(f"[Animation] 中断眼睛动画失败: {e}")
        
        # 停止行为动画
        if self.animation_integration:
            try:
                # TODO: 实现 animation_integration.interrupt_all()
                if hasattr(self.animation_integration, 'interrupt_all'):
                    self.animation_integration.interrupt_all()
                else:
                    logger.warning("[Animation] animation_integration 未实现 interrupt_all()")
            except Exception as e:
                logger.error(f"[Animation] 中断行为动画失败: {e}")
        
        self.current_animation = None
        self.current_priority = 0
        return True
    
    def get_current_animation(self) -> Optional[Dict[str, Any]]:
        """
        获取当前动画信息
        
        Returns:
            动画信息字典
        """
        if not self.current_animation:
            return None
        
        return {
            'animation': self.current_animation,
            'priority': self.current_priority
        }
