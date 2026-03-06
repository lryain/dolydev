"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

#!/usr/bin/env python3
"""
手势互动管理器

管理Doly与用户手势的互动状态，实现活泼可爱的追逐行为。

功能：
- 检测到 gesture_approach_both → 进入手势互动状态
- 实时跟踪TOF距离，保持与手的目标距离
- 动态调整运动速度和方向
- 检测到 gesture_removed → 退出手势互动状态
"""

import asyncio
import logging
import time
import yaml
from pathlib import Path
from typing import Optional, Dict, Any

logger = logging.getLogger(__name__)


class GestureInteractionManager:
    """手势互动状态管理器"""
    
    def __init__(self, daemon, config_path: str = "config/gesture_interaction.yaml"):
        self.daemon = daemon
        self.config = self._load_config(config_path)
        
        # 状态标志
        self.active = False
        self.interaction_task = None
        
        # 配置参数
        behavior_config = self.config.get('behavior', {})
        self.ignore_other_gestures = behavior_config.get('ignore_other_gestures', True)
        self.target_distance = behavior_config.get('target_distance', 50)
        self.distance_tolerance = behavior_config.get('distance_tolerance', 20)
        
        # 运动配置
        movement_config = behavior_config.get('movement', {})
        self.update_frequency = movement_config.get('update_frequency', 5)
        self.use_step_animations = movement_config.get('style', {}).get('use_step_animations', True)
        self.step_distance = movement_config.get('style', {}).get('step_distance', 2)
        self.step_pause = movement_config.get('style', {}).get('step_pause', 0.3)
        
        # 速度映射
        speed_mapping = movement_config.get('speed_mapping', {})
        self.slow_threshold = speed_mapping.get('slow_threshold', 30)
        self.slow_speed = speed_mapping.get('slow_speed', 0.15)
        self.medium_threshold = speed_mapping.get('medium_threshold', 60)
        self.medium_speed = speed_mapping.get('medium_speed', 0.3)
        self.fast_speed = speed_mapping.get('fast_speed', 0.5)
        
        # 调试标志
        debug_config = self.config.get('debug', {})
        self.log_distance = debug_config.get('log_distance', True)
        self.log_movement = debug_config.get('log_movement', True)
        self.log_state_changes = debug_config.get('log_state_changes', True)
        
        logger.info(f"[GestureInteractionManager] 初始化: 目标距离={self.target_distance}mm, "
                   f"容差=±{self.distance_tolerance}mm, 更新频率={self.update_frequency}Hz")
        
        # 验证动画配置
        animations_config = self.config.get('animations', {})
        if not animations_config:
            logger.warning("[GestureInteractionManager] ⚠️ 未找到animations配置，追逐动画将不可用")
        else:
            chase_anims = animations_config.get('chase_animations', {})
            if not chase_anims:
                logger.warning("[GestureInteractionManager] ⚠️ 未找到chase_animations配置")
            else:
                logger.info(f"[GestureInteractionManager] ✅ 追逐动画配置已加载: {list(chase_anims.keys())}")
            
            retreat_anims = animations_config.get('retreat_animations', {})
            if not retreat_anims:
                logger.warning("[GestureInteractionManager] ⚠️ 未找到retreat_animations配置，手靠近时将直接后退")
            else:
                logger.info(f"[GestureInteractionManager] ✅ 躲避动画配置已加载: {list(retreat_anims.keys())}")
    
    def _load_config(self, config_path: str) -> dict:
        """加载配置文件"""
        try:
            full_path = Path(config_path)
            if not full_path.exists():
                logger.warning(f"[GestureInteractionManager] 配置文件不存在: {config_path}, 使用默认配置")
                return {}
            
            with open(full_path, 'r', encoding='utf-8') as f:
                config = yaml.safe_load(f)
                return config.get('gesture_interaction', {})
        except Exception as e:
            logger.error(f"[GestureInteractionManager] 加载配置失败: {e}, 使用默认配置")
            return {}
    
    async def enter_interaction(self):
        """进入手势互动状态"""
        if self.active:
            logger.warning("[GestureInteractionManager] 已经在互动状态中")
            return
        
        self.active = True
        
        if self.log_state_changes:
            logger.info("[GestureInteractionManager] ✅ 进入手势互动状态")
        
        # 播放进入动画
        await self._play_enter_animation()
        
        # 启动互动循环
        self.interaction_task = asyncio.create_task(self._interaction_loop())
    
    async def exit_interaction(self):
        """退出手势互动状态"""
        if not self.active:
            return
        
        self.active = False
        
        if self.log_state_changes:
            logger.info("[GestureInteractionManager] ❌ 退出手势互动状态")
        
        # 取消互动循环
        if self.interaction_task:
            self.interaction_task.cancel()
            try:
                await self.interaction_task
            except asyncio.CancelledError:
                pass
            self.interaction_task = None
        
        # 停止所有运动
        await self._stop_movement()
        
        # 播放退出动画
        await self._play_exit_animation()
    
    async def _interaction_loop(self):
        """互动循环：持续跟踪手的位置并调整Doly的位置"""
        try:
            update_interval = 1.0 / self.update_frequency
            
            while self.active:
                # 获取当前TOF距离
                tof_data = await self._get_tof_distance()
                
                if tof_data is None:
                    await asyncio.sleep(update_interval)
                    continue
                
                left_mm, right_mm = tof_data
                avg_distance = (left_mm + right_mm) / 2
                
                if self.log_distance:
                    logger.debug(f"[GestureInteractionManager] 当前距离: L={left_mm}mm, R={right_mm}mm, Avg={avg_distance:.1f}mm")
                
                # 计算距离差异
                distance_diff = avg_distance - self.target_distance
                
                # 在容差范围内不移动
                if abs(distance_diff) <= self.distance_tolerance:
                    if self.log_movement:
                        logger.debug(f"[GestureInteractionManager] 距离合适 ({avg_distance:.1f}mm ≈ {self.target_distance}mm), 保持不动")
                    await asyncio.sleep(update_interval)
                    continue
                
                # 根据距离差异调整运动
                await self._adjust_position(distance_diff, avg_distance)
                
                await asyncio.sleep(update_interval)
        
        except asyncio.CancelledError:
            logger.info("[GestureInteractionManager] 互动循环被取消")
        except Exception as e:
            logger.error(f"[GestureInteractionManager] 互动循环异常: {e}", exc_info=True)
            self.active = False
    
    async def _get_tof_distance(self) -> Optional[tuple]:
        """获取TOF距离数据"""
        try:
            if not hasattr(self.daemon, 'tof_integrator'):
                return None
            
            tof_provider = self.daemon.tof_integrator.tof_provider
            # 从缓存中获取最新的双侧距离数据
            # TofDataProvider 有 cache 属性，cache 有 get_both() 方法
            data = tof_provider.cache.get_both()
            
            if data and isinstance(data, dict):
                left_mm = data.get('left')
                right_mm = data.get('right')
                if left_mm is not None and right_mm is not None:
                    return (left_mm, right_mm)
            
            return None
        except Exception as e:
            logger.debug(f"[GestureInteractionManager] 获取TOF数据失败: {e}")
            return None
    
    async def _adjust_position(self, distance_diff: float, avg_distance: float):
        """
        根据距离差异调整Doly的位置
        
        Args:
            distance_diff: 当前距离 - 目标距离（正=远，负=近）
            avg_distance: 平均距离
        """
        # 手太远 → Doly前进追逐
        if distance_diff > 0:
            speed = self._calculate_speed(abs(distance_diff))
            
            if self.log_movement:
                logger.info(f"[GestureInteractionManager] 手太远 ({avg_distance:.1f}mm > {self.target_distance}mm), "
                           f"前进追逐, 速度={speed:.2f}")
            
            if self.use_step_animations:
                # 使用步进式动画（活泼可爱）
                await self._execute_chase_animation()
            else:
                # 直接控制电机
                await self._move_forward(speed, duration=0.5)
        
        # 手太近 → Doly后退躲避
        else:
            speed = self._calculate_speed(abs(distance_diff))
            
            if self.log_movement:
                logger.info(f"[GestureInteractionManager] 手太近 ({avg_distance:.1f}mm < {self.target_distance}mm), "
                           f"后退躲避, 速度={speed:.2f}")
            
            if self.use_step_animations:
                # 使用步进式后退动画（配对前进追逐）
                await self._execute_retreat_animation()
            else:
                # 直接控制电机后退
                await self._move_backward(speed, duration=0.5)
    
    def _calculate_speed(self, distance_diff: float) -> float:
        """
        根据距离差异计算速度
        
        距离差越大，速度越快
        """
        if distance_diff < self.slow_threshold:
            return self.slow_speed
        elif distance_diff < self.medium_threshold:
            return self.medium_speed
        else:
            return self.fast_speed
    
    async def _execute_chase_animation(self):
        """执行追逐动画（go_forward_step.xml）"""
        try:
            current_state = self.daemon.state_machine.current_state.name
            animations_config = self.config.get('animations', {}).get('chase_animations', {})
            
            # 根据当前状态获取动画配置
            anim_config = animations_config.get(current_state, animations_config.get('ANY', {}))
            
            if not anim_config:
                logger.warning(f"[GestureInteractionManager] 未找到追逐动画配置 (状态: {current_state}, 可用: {list(animations_config.keys())})")
                logger.debug(f"[GestureInteractionManager] 配置内容: {animations_config}")
                return
            
            action = anim_config.get('action')
            
            if action == 'play_animation':
                animation_file = anim_config.get('animation')
                if animation_file:
                    await self.daemon.animation_integration.play_animation_by_file(animation_file)
                    if self.log_movement:
                        logger.info(f"[GestureInteractionManager] 执行追逐动画: {animation_file}")
            
            elif action == 'play_animation_category':
                category = anim_config.get('category')
                level = anim_config.get('level', 1)
                if category:
                    await self.daemon.animation_integration.play_animation_by_category(category, level)
                    if self.log_movement:
                        logger.info(f"[GestureInteractionManager] 执行追逐动画: {category} L{level}")
            
            # 添加步进停顿
            await asyncio.sleep(self.step_pause)
        
        except Exception as e:
            logger.error(f"[GestureInteractionManager] 执行追逐动画失败: {e}")
    
    async def _execute_retreat_animation(self):
        """执行后退躲避动画"""
        try:
            current_state = self.daemon.state_machine.current_state.name
            animations_config = self.config.get('animations', {}).get('retreat_animations', {})
            
            # 根据当前状态获取动画配置
            anim_config = animations_config.get(current_state, animations_config.get('ANY', {}))
            
            if not anim_config:
                logger.debug(f"[GestureInteractionManager] 未找到后退躲避动画配置 (状态: {current_state})")
                # 如果没有躲避动画配置，直接后退
                await self._move_backward(0.4, duration=0.5)
                return
            
            action = anim_config.get('action')
            
            if action == 'play_animation':
                animation_file = anim_config.get('animation')
                if animation_file:
                    await self.daemon.animation_integration.play_animation_by_file(animation_file)
                    if self.log_movement:
                        logger.info(f"[GestureInteractionManager] 执行躲避动画: {animation_file}")
            
            elif action == 'play_animation_category':
                category = anim_config.get('category')
                level = anim_config.get('level', 1)
                if category:
                    await self.daemon.animation_integration.play_animation_by_category(category, level)
                    if self.log_movement:
                        logger.info(f"[GestureInteractionManager] 执行躲避动画: {category} L{level}")
            
            # 添加步进停顿
            await asyncio.sleep(self.step_pause)
        
        except Exception as e:
            logger.error(f"[GestureInteractionManager] 执行躲避动画失败: {e}")
    
    async def _move_forward(self, speed: float, duration: float):
        """前进移动"""
        try:
            # 获取动画集成的接口对象
            anim_integration = self.daemon.animation_integration
            if not anim_integration or not hasattr(anim_integration, 'interfaces'):
                logger.warning("[GestureInteractionManager] 动画集成接口未初始化")
                return
            
            interfaces = anim_integration.interfaces
            if not interfaces or not hasattr(interfaces, 'drive'):
                logger.warning("[GestureInteractionManager] drive接口未初始化")
                return
            
            drive_interface = interfaces.drive
            await drive_interface.motor_forward(speed, duration)
        except Exception as e:
            logger.error(f"[GestureInteractionManager] 前进失败: {e}")
    
    async def _move_backward(self, speed: float, duration: float):
        """后退移动"""
        try:
            # 获取动画集成的接口对象
            anim_integration = self.daemon.animation_integration
            if not anim_integration or not hasattr(anim_integration, 'interfaces'):
                logger.warning("[GestureInteractionManager] 动画集成接口未初始化")
                return
            
            interfaces = anim_integration.interfaces
            if not interfaces or not hasattr(interfaces, 'drive'):
                logger.warning("[GestureInteractionManager] drive接口未初始化")
                return
            
            drive_interface = interfaces.drive
            await drive_interface.motor_backward(speed, duration)
        except Exception as e:
            logger.error(f"[GestureInteractionManager] 后退失败: {e}")
    
    async def _stop_movement(self):
        """停止运动"""
        try:
            # 获取动画集成的接口对象
            anim_integration = self.daemon.animation_integration
            if not anim_integration or not hasattr(anim_integration, 'interfaces'):
                logger.warning("[GestureInteractionManager] 动画集成接口未初始化")
                return
            
            interfaces = anim_integration.interfaces
            if not interfaces or not hasattr(interfaces, 'drive'):
                logger.warning("[GestureInteractionManager] drive接口未初始化")
                return
            
            drive_interface = interfaces.drive
            await drive_interface.stop()
            logger.info("[GestureInteractionManager] 停止运动")
        except Exception as e:
            logger.error(f"[GestureInteractionManager] 停止运动失败: {e}")
    
    async def _play_enter_animation(self):
        """播放进入动画"""
        try:
            current_state = self.daemon.state_machine.current_state.name
            animations_config = self.config.get('animations', {}).get('enter', {})
            
            anim_config = animations_config.get(current_state, animations_config.get('ANY', {}))
            
            if not anim_config:
                return
            
            action = anim_config.get('action')
            
            if action == 'play_animation':
                animation_file = anim_config.get('animation')
                if animation_file:
                    await self.daemon.animation_integration.play_animation_by_file(animation_file)
                    logger.info(f"[GestureInteractionManager] 播放进入动画: {animation_file}")
            
            elif action == 'play_animation_category':
                category = anim_config.get('category')
                level = anim_config.get('level', 1)
                if category:
                    await self.daemon.animation_integration.play_animation_by_category(category, level)
                    logger.info(f"[GestureInteractionManager] 播放进入动画: {category} L{level}")
        
        except Exception as e:
            logger.error(f"[GestureInteractionManager] 播放进入动画失败: {e}")
    
    async def _play_exit_animation(self):
        """播放退出动画"""
        try:
            current_state = self.daemon.state_machine.current_state.name
            animations_config = self.config.get('animations', {}).get('exit', {})
            
            anim_config = animations_config.get(current_state, animations_config.get('ANY', {}))
            
            if not anim_config:
                return
            
            action = anim_config.get('action')
            
            if action == 'play_animation':
                animation_file = anim_config.get('animation')
                if animation_file:
                    await self.daemon.animation_integration.play_animation_by_file(animation_file)
                    logger.info(f"[GestureInteractionManager] 播放退出动画: {animation_file}")
            
            elif action == 'play_animation_category':
                category = anim_config.get('category')
                level = anim_config.get('level', 1)
                if category:
                    await self.daemon.animation_integration.play_animation_by_category(category, level)
                    logger.info(f"[GestureInteractionManager] 播放退出动画: {category} L{level}")
        
        except Exception as e:
            logger.error(f"[GestureInteractionManager] 播放退出动画失败: {e}")
    
    def should_ignore_gesture(self, gesture_name: str) -> bool:
        """
        判断是否应该忽略某个手势
        
        在互动状态下，如果配置了ignore_other_gestures=True，
        则忽略除了exit_event之外的所有手势
        """
        if not self.active:
            return False
        
        if not self.ignore_other_gestures:
            return False
        
        # 不忽略退出事件
        exit_event = self.config.get('triggers', {}).get('exit_event', 'gesture_removed')
        if gesture_name == exit_event:
            return False
        
        # 忽略其他所有手势
        return True
