"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

#!/usr/bin/env python3
"""
TOF Daemon集成模块

将TOF数据提供器集成到doly daemon中，提供：
- 避障功能
- 手势识别
- 状态感知的TOF行为
- ZMQ事件发布
"""

import asyncio
import zmq
import json
import time
import logging
from typing import Dict, Any, List

from .tof_data_provider import TofDataProvider

logger = logging.getLogger(__name__)


class TofDaemonIntegrator:
    """TOF Daemon集成器"""
    
    def __init__(self, daemon_instance, config: Dict[str, Any]):
        self.daemon = daemon_instance
        self.config = config
        self.tof_config = config.get('tof_integration', {})
        
        # TOF数据提供器
        self.tof_provider = None
        
        # ZMQ发布器
        self.zmq_context = daemon_instance.zmq_context if hasattr(daemon_instance, 'zmq_context') else None
        self.event_publisher = None
        
        # 集成状态
        self.enabled = self.tof_config.get('enabled', True)
        self.initialized = False
        
        # 状态感知配置
        self.state_behaviors = self.tof_config.get('state_behaviors', {})
        
        logger.info(f"[TofDaemonIntegrator] 初始化: enabled={self.enabled}")
    
    async def initialize(self):
        """初始化TOF集成"""
        if not self.enabled:
            logger.info("[TofDaemonIntegrator] TOF集成已禁用")
            return True
        
        try:
            # 初始化TOF数据提供器
            self.tof_provider = TofDataProvider(self.tof_config)
            await self.tof_provider.initialize()
            
            # 注册事件回调
            self.tof_provider.register_event_callback(self._handle_tof_event)
            
            # 设置ZMQ发布器
            if self.zmq_context:
                self.event_publisher = self.zmq_context.socket(zmq.PUB)
                self.event_publisher.bind("tcp://*:5566")  # TOF事件发布端口
                logger.info("[TofDaemonIntegrator] ZMQ事件发布器已初始化")
            
            self.initialized = True
            logger.info("[TofDaemonIntegrator] 初始化完成")
            return True
            
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 初始化失败: {e}")
            return False
    
    def start(self):
        """启动TOF集成服务"""
        if not self.enabled or not self.initialized:
            return
        
        # 启动后台读取
        self.tof_provider.start_background_reader()
        logger.info("[TofDaemonIntegrator] TOF后台服务已启动")
    
    def stop(self):
        """停止TOF集成服务"""
        if self.tof_provider:
            self.tof_provider.stop_background_reader()
        
        if self.event_publisher:
            self.event_publisher.close()
        
        logger.info("[TofDaemonIntegrator] TOF集成服务已停止")
    
    def _handle_tof_event(self, events: List[str], data: Dict[str, int]):
        """处理TOF事件"""
        try:
            # 获取当前daemon状态
            if hasattr(self.daemon, 'state_machine') and hasattr(self.daemon.state_machine, 'current_state'):
                current_state = self.daemon.state_machine.current_state.value
            else:
                current_state = 'IDLE'  # 默认为IDLE而不是unknown
            
            for event in events:
                # 状态感知的事件处理
                self._handle_state_aware_event(event, data, current_state)
                
                # 发布ZMQ事件
                self._publish_tof_event(event, data, current_state)
            
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 事件处理失败: {e}")
    
    def _handle_state_aware_event(self, event: str, data: Dict[str, int], state: str):
        """状态感知的事件处理"""
        try:
            # 避障事件处理
            if event.startswith('obstacle_emergency'):
                self._handle_emergency_obstacle(event, data, state)
            elif event.startswith('obstacle_warning'):
                self._handle_warning_obstacle(event, data, state)
            
            # 手势事件处理
            elif event.startswith('gesture_'):
                self._handle_gesture_event(event, data, state)
                
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 状态感知事件处理失败: {e}")
    
    def _handle_emergency_obstacle(self, event: str, data: Dict[str, int], state: str):
        """处理紧急避障事件"""
        logger.warning(f"[TofDaemonIntegrator] 紧急避障: {event}, 状态: {state}, 距离: {data}")
        
        # 根据状态决定行为
        if state in ['moving', 'exploring']:
            # 立即停止移动
            self._trigger_emergency_stop()
            
            # 播放警告音效
            self._play_obstacle_alert()
            
            # 根据方向执行避障动作
            if 'left' in event:
                self._execute_avoidance_behavior('turn_right')
            elif 'right' in event:
                self._execute_avoidance_behavior('turn_left')
            elif 'both' in event:
                self._execute_avoidance_behavior('back_away')
                
        elif state == 'idle':
            # 静止状态只播放提示音
            self._play_obstacle_alert()
    
    def _handle_warning_obstacle(self, event: str, data: Dict[str, int], state: str):
        """处理警告级别的避障事件"""
        logger.info(f"[TofDaemonIntegrator] 避障警告: {event}, 状态: {state}, 距离: {data}")
        
        # 根据状态决定行为
        if state == 'exploring':
            # 探索时减速并准备转向
            self._reduce_movement_speed()
            
            # 播放轻微提示音
            self._play_warning_sound()
            
        elif state == 'moving':
            # 移动中给出警告但不强制停止
            self._play_warning_sound()
    
    def _handle_gesture_event(self, event: str, data: Dict[str, int], state: str):
        """处理手势事件（支持状态感知的映射）"""
        logger.info(f"[TofDaemonIntegrator] 手势识别: {event}, 状态: {state}, 距离: {data}")
        
        # ★ 新增：处理特殊手势事件（进入/退出手势互动状态）
        if event == 'gesture_approach_both':
            self._handle_gesture_approach_both()
            return
        elif event == 'gesture_removed':
            self._handle_gesture_removed()
            return
        
        # ★ 新增：检查是否应该忽略手势（在手势互动状态中）
        if hasattr(self.daemon, 'gesture_interaction_manager') and self.daemon.gesture_interaction_manager:
            gesture_type = event.replace('gesture_', '')
            if self.daemon.gesture_interaction_manager.should_ignore_gesture(gesture_type):
                logger.debug(f"[TofDaemonIntegrator] 手势互动状态中，忽略手势: {gesture_type}")
                return
        
        # 手势行为映射（支持状态感知）
        gesture_behaviors = self.tof_config.get('gesture_behaviors', {})
        
        # 解析手势类型（移除gesture_前缀）
        gesture_type = event.replace('gesture_', '')
        
        # 获取该手势的状态感知映射
        gesture_config = gesture_behaviors.get(gesture_type, {})
        
        # 优先使用当前状态的映射，否则使用ANY
        action_config = gesture_config.get(state) or gesture_config.get('ANY')
        
        if action_config:
            logger.info(f"[TofDaemonIntegrator] 执行手势行为（{state}状态）: {gesture_type}")
            self._execute_gesture_action(action_config, gesture_type)
        else:
            logger.warning(f"[TofDaemonIntegrator] 未找到手势 {gesture_type} 在状态 {state} 的映射")
    
    def _execute_gesture_action(self, action_config: Dict[str, Any], gesture_type: str):
        """执行手势对应的动作"""
        try:
            action_type = action_config.get('action', 'play_animation')
            
            if action_type == 'play_animation':
                # 播放单个动画
                animation = action_config.get('animation')
                if animation:
                    logger.info(f"[TofDaemonIntegrator] 手势->播放动画: {animation}")
                    self._execute_gesture_animation(animation)
                else:
                    logger.warning(f"[TofDaemonIntegrator] 缺少animation字段")
            
            elif action_type == 'play_animation_category':
                # 播放动画分类
                category = action_config.get('category')
                level = action_config.get('level', 1)
                logger.info(f"[TofDaemonIntegrator] 手势->播放动画分类: {category} level={level}")
                self._execute_gesture_animation_category(category, level)
            
            elif action_type == 'direct_command':
                # 执行直接命令（如move_forward, move_backward）
                target = action_config.get('target')
                command = action_config.get('command')
                params = action_config.get('params', {})
                logger.info(f"[TofDaemonIntegrator] 手势->执行命令: {target}.{command}")
                self._execute_direct_command_gesture(target, command, params)
            
            else:
                logger.warning(f"[TofDaemonIntegrator] 未知的手势action_type: {action_type}")
        
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 执行手势动作异常: {e}", exc_info=True)
    
    def _execute_gesture_animation(self, animation_file: str):
        """通过animation_integration播放手势动画"""
        try:
            anim_integration = getattr(self.daemon, 'animation_integration', None)
            if anim_integration and getattr(anim_integration, '_initialized', False):
                loop = getattr(self.daemon, 'loop', None)
                coro = anim_integration.play_animation_by_file(animation_file)
                import asyncio
                if loop and getattr(loop, 'is_running', lambda: False)():
                    try:
                        loop.create_task(coro)
                    except Exception:
                        asyncio.run_coroutine_threadsafe(coro, loop)
                else:
                    try:
                        asyncio.run(coro)
                    except Exception:
                        loop2 = asyncio.new_event_loop()
                        asyncio.set_event_loop(loop2)
                        loop2.run_until_complete(coro)
                        loop2.close()
                logger.info(f"[TofDaemonIntegrator] 已调度手势动画: {animation_file}")
                return
        except Exception as e:
            logger.warning(f"[TofDaemonIntegrator] 调度动画失败: {e}")
        
        # 回退方案
        if hasattr(self.daemon, 'animation_system'):
            try:
                self.daemon.animation_system.play_animation(animation_file)
                logger.info(f"[TofDaemonIntegrator] 手势动画(fallback): {animation_file}")
            except Exception as e:
                logger.error(f"[TofDaemonIntegrator] 回退动画失败: {e}")
    
    def _execute_gesture_animation_category(self, category: str, level: int = 1):
        """播放手势动画分类"""
        logger.info(f"[TofDaemonIntegrator] 手势播放分类动画: {category} level={level}")
        # TODO: 实现从animationlist.xml中随机选择该分类的动画
    
    def _execute_direct_command_gesture(self, target: str, command: str, params: Dict[str, Any]):
        """执行手势对应的直接命令（如move_forward）"""
        try:
            if target == 'drive':
                if command == 'move_forward':
                    duration = params.get('duration', 2.0)
                    speed = params.get('speed', 0.2)
                    logger.info(f"[TofDaemonIntegrator] 手势->前进: {duration}s @ {speed}速度")
                    # TODO: 调用daemon的move_forward方法
                elif command == 'move_backward':
                    duration = params.get('duration', 2.0)
                    speed = params.get('speed', 0.2)
                    logger.info(f"[TofDaemonIntegrator] 手势->后退: {duration}s @ {speed}速度")
                    # TODO: 调用daemon的move_backward方法
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 执行手势命令异常: {e}")

    def _handle_gesture_approach_both(self):
        """处理双手靠近手势 - 进入手势互动状态"""
        try:
            if not hasattr(self.daemon, 'gesture_interaction_manager') or not self.daemon.gesture_interaction_manager:
                # 延迟初始化手势互动管理器
                if hasattr(self.daemon, '_setup_gesture_interaction_manager'):
                    self.daemon._setup_gesture_interaction_manager()
            
            if self.daemon.gesture_interaction_manager:
                # 进入手势互动状态
                loop = getattr(self.daemon, 'loop', None)
                coro = self.daemon.gesture_interaction_manager.enter_interaction()
                
                import asyncio
                if loop and getattr(loop, 'is_running', lambda: False)():
                    try:
                        loop.create_task(coro)
                    except Exception:
                        asyncio.run_coroutine_threadsafe(coro, loop)
                else:
                    try:
                        asyncio.run(coro)
                    except Exception:
                        loop2 = asyncio.new_event_loop()
                        asyncio.set_event_loop(loop2)
                        loop2.run_until_complete(coro)
                        loop2.close()
                
                # 切换到手势互动状态 (使用 transition_to 而不是 change_state)
                if hasattr(self.daemon, 'state_machine'):
                    from .state_machine import DolyState
                    self.daemon.state_machine.transition_to(DolyState.GESTURE_INTERACTION)
                
                logger.info("[TofDaemonIntegrator] ✅ 进入手势互动状态 (gesture_approach_both)")
            else:
                logger.warning("[TofDaemonIntegrator] 手势互动管理器未初始化")
                
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 处理gesture_approach_both失败: {e}", exc_info=True)
    
    def _handle_gesture_removed(self):
        """处理手势移除 - 退出手势互动状态"""
        try:
            if hasattr(self.daemon, 'gesture_interaction_manager') and self.daemon.gesture_interaction_manager:
                # 退出手势互动状态
                loop = getattr(self.daemon, 'loop', None)
                coro = self.daemon.gesture_interaction_manager.exit_interaction()
                
                import asyncio
                if loop and getattr(loop, 'is_running', lambda: False)():
                    try:
                        loop.create_task(coro)
                    except Exception:
                        asyncio.run_coroutine_threadsafe(coro, loop)
                else:
                    try:
                        asyncio.run(coro)
                    except Exception:
                        loop2 = asyncio.new_event_loop()
                        asyncio.set_event_loop(loop2)
                        loop2.run_until_complete(coro)
                        loop2.close()
                
                # 返回到之前的状态（使用 transition_to 而不是 change_state）
                if hasattr(self.daemon, 'state_machine'):
                    from .state_machine import DolyState
                    # 简单返回IDLE，后续可以增强为记忆上一个状态
                    self.daemon.state_machine.transition_to(DolyState.IDLE)
                
                logger.info("[TofDaemonIntegrator] ❌ 退出手势互动状态 (gesture_removed)")
            else:
                logger.debug("[TofDaemonIntegrator] 手势互动管理器未活跃，忽略gesture_removed")
                
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 处理gesture_removed失败: {e}", exc_info=True)
    
    def _publish_tof_event(self, event: str, data: Dict[str, int], state: str):
        """发布TOF事件到ZMQ"""
        if not self.event_publisher:
            return
        
        try:
            message = {
                'type': 'tof_event',
                'event': event,
                'data': data,
                'state': state,
                'timestamp': time.time()
            }
            
            self.event_publisher.send_string(json.dumps(message))
            
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] ZMQ发布失败: {e}")
    
    # 行为执行方法（调用daemon或其他模块）
    def _trigger_emergency_stop(self):
        """触发紧急停止"""
        try:
            if hasattr(self.daemon, 'motor_controller'):
                # 使用新的电机刹车功能 - 同步调用
                self.daemon.motor_controller.emergency_brake_sync()
                logger.info("[TofDaemonIntegrator] 触发紧急刹车")
            elif hasattr(self.daemon, 'publish_motor_command'):
                # 备用方法：直接发送停止命令
                self.daemon.publish_motor_command({
                    'command': 'stop',
                    'brake': True,
                    'priority': 'emergency'
                })
                logger.info("[TofDaemonIntegrator] 发送紧急停止命令")
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 紧急停止失败: {e}")
    
    def _execute_avoidance_behavior(self, behavior: str):
        """执行避障行为"""
        try:
            behavior_map = {
                'turn_right': {'action': 'turn', 'direction': 'right', 'angle': 45},
                'turn_left': {'action': 'turn', 'direction': 'left', 'angle': 45},
                'back_away': {'action': 'move', 'direction': 'backward', 'distance': 100}
            }
            
            if behavior in behavior_map:
                params = behavior_map[behavior]
                if hasattr(self.daemon, 'execute_movement_sync'):
                    self.daemon.execute_movement_sync(params)
                    logger.info(f"[TofDaemonIntegrator] 执行避障行为: {behavior}")
                elif hasattr(self.daemon, 'publish_motor_command'):
                    # 转换为电机命令
                    self.daemon.publish_motor_command({
                        'command': params['action'],
                        **params
                    })
                    logger.info(f"[TofDaemonIntegrator] 发布避障命令: {behavior}")
                    
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 避障行为执行失败: {e}")
    
    def _reduce_movement_speed(self):
        """减少移动速度"""
        try:
            if hasattr(self.daemon, 'motor_controller'):
                # 设置较低的速度 - 同步调用
                self.daemon.motor_controller.set_speed_limit_sync(50)  # 50%速度
                logger.info("[TofDaemonIntegrator] 降低移动速度")
            elif hasattr(self.daemon, 'publish_motor_command'):
                self.daemon.publish_motor_command({
                    'command': 'set_speed_limit',
                    'limit': 50
                })
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 速度调节失败: {e}")
    
    def _play_obstacle_alert(self):
        """播放障碍物警告音效"""
        try:
            if hasattr(self.daemon, 'audio_player'):
                self.daemon.audio_player.play_sound('obstacle_warning')
            elif hasattr(self.daemon, 'publish_audio_command'):
                self.daemon.publish_audio_command({
                    'action': 'play',
                    'sound': 'obstacle_warning',
                    'priority': 'high'
                })
            logger.debug("[TofDaemonIntegrator] 播放障碍物警告音效")
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 音效播放失败: {e}")
    
    def _play_warning_sound(self):
        """播放轻微警告音效"""
        try:
            if hasattr(self.daemon, 'audio_player'):
                self.daemon.audio_player.play_sound('proximity_beep')
            logger.debug("[TofDaemonIntegrator] 播放轻微警告音效")
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 警告音效播放失败: {e}")
    
    def _execute_gesture_response(self, behavior: str, side: str):
        """执行手势响应"""
        try:
            # 优先使用新的 AnimationIntegration 异步接口
            anim_integration = getattr(self.daemon, 'animation_integration', None)
            if anim_integration and getattr(anim_integration, '_initialized', False):
                # 如果 daemon 提供事件循环，优先将协程提交到该循环
                try:
                    loop = getattr(self.daemon, 'loop', None)
                    coro = anim_integration.play_animation_by_file(behavior)
                    # 如果 loop 存在并且正在运行，使用 run_coroutine_threadsafe 或 create_task
                    import asyncio
                    if loop and getattr(loop, 'is_running', lambda: False)():
                        try:
                            # 若 loop 在本进程中运行，使用 create_task
                            loop.create_task(coro)
                        except Exception:
                            # 否则使用线程安全的提交
                            asyncio.run_coroutine_threadsafe(coro, loop)
                    else:
                        # 回退方案：在当前线程同步执行协程
                        try:
                            asyncio.run(coro)
                        except Exception:
                            # 最后手段：在新事件循环中运行
                            loop2 = asyncio.new_event_loop()
                            asyncio.set_event_loop(loop2)
                            loop2.run_until_complete(coro)
                            loop2.close()
                    logger.info(f"[TofDaemonIntegrator] 已调度手势响应动画: {behavior} ({side})")
                    return
                except Exception as e:
                    logger.warning(f"[TofDaemonIntegrator] 调度动画失败，尝试回退: {e}")

            # 回退到旧的同步/模拟接口（例如单元测试中的 MockDaemon）
            if hasattr(self.daemon, 'animation_system'):
                try:
                    self.daemon.animation_system.play_animation(behavior, {'side': side})
                    logger.info(f"[TofDaemonIntegrator] 执行手势响应 (fallback): {behavior} ({side})")
                    return
                except Exception as e:
                    logger.error(f"[TofDaemonIntegrator] 回退播放动画失败: {e}")
                
        except Exception as e:
            logger.error(f"[TofDaemonIntegrator] 手势响应执行失败: {e}")
    
    # 外部查询接口
    async def get_distance_data(self) -> Dict[str, Any]:
        """获取当前距离数据"""
        if not self.tof_provider:
            return None
        
        data = await self.tof_provider.get_both_distances()
        stats = self.tof_provider.get_statistics()
        
        return {
            'distances': data,
            'statistics': stats,
            'healthy': self.tof_provider.is_healthy()
        }
    
    async def get_status(self) -> Dict[str, Any]:
        """获取TOF集成状态"""
        if not self.tof_provider:
            return {'enabled': False, 'initialized': False}
        
        return {
            'enabled': self.enabled,
            'initialized': self.initialized,
            'provider_healthy': self.tof_provider.is_healthy(),
            'statistics': self.tof_provider.get_statistics()
        }
    
    def update_config(self, new_config: Dict[str, Any]):
        """动态更新配置"""
        self.tof_config.update(new_config)
        
        if self.tof_provider:
            # 更新事件检测器配置
            self.tof_provider.event_detector.config = self.tof_config
            logger.info("[TofDaemonIntegrator] 配置已更新")


# Daemon扩展方法
def extend_daemon_with_tof(daemon_class):
    """为Daemon类添加TOF功能"""
    
    async def init_tof_integration(self):
        """初始化TOF集成"""
        tof_config = self.config.get('tof_integration', {})
        
        if tof_config.get('enabled', False):
            self.tof_integrator = TofDaemonIntegrator(self, self.config)
            success = await self.tof_integrator.initialize()
            
            if success:
                self.tof_integrator.start()
                logger.info("[Daemon] TOF集成已启动")
            else:
                logger.error("[Daemon] TOF集成初始化失败")
        else:
            logger.info("[Daemon] TOF集成已禁用")
    
    def stop_tof_integration(self):
        """停止TOF集成"""
        if hasattr(self, 'tof_integrator'):
            self.tof_integrator.stop()
    
    async def handle_tof_command(self, command: Dict[str, Any]):
        """处理TOF相关命令"""
        if not hasattr(self, 'tof_integrator'):
            return {'error': 'TOF集成未初始化'}
        
        cmd_type = command.get('type')
        
        if cmd_type == 'get_distance':
            return await self.tof_integrator.get_distance_data()
        elif cmd_type == 'get_status':
            return await self.tof_integrator.get_status()
        elif cmd_type == 'update_config':
            new_config = command.get('config', {})
            self.tof_integrator.update_config(new_config)
            return {'success': True}
        elif cmd_type == 'play_gesture_animation':
            # 手动触发已映射的手势动画，用于测试
            gesture = command.get('gesture')
            if not gesture:
                return {'error': 'missing gesture'}
            mapping = self.config.get('tof_integration', {}).get('gesture_behaviors', {})
            anim = mapping.get(gesture)
            if not anim:
                return {'error': f'no animation mapped for {gesture}'}

            # 如果 integrator 已初始化，使用其动画系统引用
            try:
                # 优先使用 daemon.animation_integration（异步接口）
                anim_integration = getattr(self, 'animation_integration', None)
                if anim_integration and getattr(anim_integration, '_initialized', False):
                    try:
                        ok = await anim_integration.play_animation_by_file(anim)
                        if ok:
                            return {'success': True, 'animation': anim}
                        else:
                            return {'error': 'play_animation returned False'}
                    except Exception as e:
                        return {'error': str(e)}

                # 回退到 integrator.daemon 的旧接口（例如 MockDaemon）
                if hasattr(self, 'tof_integrator') and self.tof_integrator and hasattr(self.tof_integrator.daemon, 'animation_system'):
                    try:
                        self.tof_integrator.daemon.animation_system.play_animation(anim, {'source': 'tof_test', 'gesture': gesture})
                        return {'success': True, 'animation': anim}
                    except Exception as e:
                        return {'error': str(e)}

                return {'error': 'animation system unavailable'}
            except Exception as e:
                return {'error': str(e)}
        else:
            return {'error': f'未知的TOF命令: {cmd_type}'}
    
    # 添加方法到daemon类
    daemon_class.init_tof_integration = init_tof_integration
    daemon_class.stop_tof_integration = stop_tof_integration
    daemon_class.handle_tof_command = handle_tof_command
    
    return daemon_class