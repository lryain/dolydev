"""
Doly 探索模式管理器

负责 EXPLORING 状态下的自主巡航行为
- 可配置的巡航策略
- 悬崖避障（4个传感器）
- 障碍物检测（ToF）
- 与状态机和事件总线的集成

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import asyncio
import json
import logging
import random
import time
import yaml
from typing import Dict, Any, Optional
from pathlib import Path
from enum import Enum
from dataclasses import dataclass

logger = logging.getLogger(__name__)


class ExplorationState(Enum):
    """探索模式内部状态"""
    IDLE = "idle"              # 等待启动
    MOVING = "moving"          # 直线移动
    TURNING = "turning"        # 转向
    PAUSING = "pausing"        # 停顿
    OBSTACLE_AVOIDING = "avoiding"  # 避障
    EMERGENCY_STOP = "emergency"    # 紧急停止


@dataclass
class SensorSnapshot:
    """传感器数据快照"""
    cliff_fl: bool = False      # 前左悬崖
    cliff_fr: bool = False      # 前右悬崖
    cliff_bl: bool = False      # 后左悬崖
    cliff_br: bool = False      # 后右悬崖
    tof_left: Optional[int] = None    # 左ToF距离 (mm)
    tof_right: Optional[int] = None   # 右ToF距离 (mm)
    battery: Optional[float] = None   # 电池电量 (%)
    
    def has_cliff(self) -> bool:
        """是否检测到任何悬崖"""
        return any([self.cliff_fl, self.cliff_fr, self.cliff_bl, self.cliff_br])
    
    def cliff_count(self) -> int:
        """检测到的悬崖数量"""
        return sum([self.cliff_fl, self.cliff_fr, self.cliff_bl, self.cliff_br])
    
    def has_obstacle(self, threshold: int) -> bool:
        """是否检测到障碍物"""
        if self.tof_left and self.tof_left < threshold:
            return True
        if self.tof_right and self.tof_right < threshold:
            return True
        return False


class ExplorationManager:
    """探索模式管理器"""
    
    def __init__(self, config_path: str = "config/exploration.yaml", 
                 drive_interface=None, event_bus=None):
        """
        初始化探索管理器
        
        Args:
            config_path: 配置文件路径
            drive_interface: DriveSystemInterface 实例
            event_bus: EventBus 实例
        """
        self.drive = drive_interface
        self.event_bus = event_bus
        self.state = ExplorationState.IDLE
        self.running = False
        
        # 加载配置
        self.config = self._load_config(config_path)
        logger.info(f"[ExplorationManager] 配置已加载: {config_path}")
        
        # 传感器状态
        self.sensor_snapshot = SensorSnapshot()
        self.last_cliff_time = {}  # 悬崖事件冷却时间
        self.last_obstacle_time = 0.0
        
        # 统计信息
        self.stats = {
            "total_moves": 0,
            "total_turns": 0,
            "cliff_avoidances": 0,
            "obstacle_avoidances": 0,
            "start_time": None,
        }
    
    def _load_config(self, config_path: str) -> Dict[str, Any]:
        """加载配置文件"""
        path = Path(config_path)
        if not path.exists():
            logger.warning(f"[ExplorationManager] 配置文件不存在: {config_path}，使用默认配置")
            return {"exploration": {}}
        
        with open(path, 'r', encoding='utf-8') as f:
            config = yaml.safe_load(f)
            return config
    
    def get_exploration_config(self) -> Dict[str, Any]:
        """获取探索配置"""
        return self.config.get('exploration', {})
    
    async def start(self):
        """启动探索模式"""
        if self.running:
            logger.warning("[ExplorationManager] 探索模式已在运行")
            return
        
        self.running = True
        self.state = ExplorationState.IDLE
        self.stats['start_time'] = time.time()
        
        logger.info("[ExplorationManager] ✓ 探索模式已启动")
        
        # 订阅传感器事件
        if self.event_bus:
            self._subscribe_to_events()
        
        # 启动巡航循环
        asyncio.create_task(self._exploration_loop())
    
    async def stop(self):
        """停止探索模式"""
        if not self.running:
            return
        
        self.running = False
        self.state = ExplorationState.EMERGENCY_STOP
        
        # 停止所有运动
        if self.drive:
            await self.drive.stop()
        
        logger.info(f"[ExplorationManager] ✓ 探索模式已停止 (总耗时: {self._get_elapsed_time():.0f}s)")
        self._log_stats()
    
    def _subscribe_to_events(self):
        """订阅传感器事件"""
        # 注意：传感器事件是通过 ZMQ 主题订阅的，而不是通过 EventBus 的标准事件类型
        # 这些回调方法由 ZMQEventSubscriber 或其他模块直接调用
        # 这里只是作为文档和占位符
        
        logger.info("[ExplorationManager] ✓ 传感器事件监听已配置")
    
    async def _exploration_loop(self):
        """主巡航循环"""
        try:
            config = self.get_exploration_config()
            cruise_cfg = config.get('cruise', {})
            safety_cfg = config.get('safety', {})
            
            # 检查安全限制
            max_run_time = safety_cfg.get('max_continuous_run_time', 300)
            
            while self.running:
                # 检查运行时间
                elapsed = self._get_elapsed_time()
                if elapsed > max_run_time:
                    logger.warning(f"[ExplorationManager] 超过最大运行时间 ({elapsed:.0f}s > {max_run_time}s)，停止探索")
                    await self.stop()
                    break
                
                # 检查电池
                if self.sensor_snapshot.battery and self.sensor_snapshot.battery < safety_cfg.get('battery_threshold', 20):
                    logger.warning(f"[ExplorationManager] 电量过低 ({self.sensor_snapshot.battery:.1f}%)，停止探索")
                    await self.stop()
                    break
                
                # 获取下一个动作
                action = self._decide_next_action()
                
                # 执行动作
                await self._execute_action(action, cruise_cfg)
                
                # 短暂等待避免CPU占用过高
                await asyncio.sleep(0.1)
        
        except asyncio.CancelledError:
            logger.info("[ExplorationManager] 探索循环被取消")
        except Exception as e:
            logger.error(f"[ExplorationManager] 探索循环发生错误: {e}", exc_info=True)
            await self.stop()
    
    def _clear_cliff_state(self):
        """清除所有悬崖状态"""
        self.sensor_snapshot.cliff_fl = False
        self.sensor_snapshot.cliff_fr = False
        self.sensor_snapshot.cliff_bl = False
        self.sensor_snapshot.cliff_br = False
        logger.debug("[ExplorationManager] 悬崖状态已清除")
    
    def _decide_next_action(self) -> Dict[str, Any]:
        """决定下一个动作"""
        config = self.get_exploration_config()
        
        # 优先级1: 处理紧急状况（多个悬崖）
        if self.sensor_snapshot.cliff_count() >= 2:
            logger.info(f"[ExplorationManager] 检测到 {self.sensor_snapshot.cliff_count()} 个悬崖，执行紧急避障")
            return {
                'type': 'cliff_avoid',
                'action': 'multiple_detected',
                'priority': 'critical'
            }
        
        # 优先级2: 处理单个悬崖
        if self.sensor_snapshot.has_cliff():
            action = self._get_cliff_action()
            if action:
                logger.info(f"[ExplorationManager] 检测到悬崖，执行避障: {action}")
                return {
                    'type': 'cliff_avoid',
                    'action': action,
                    'priority': 'high'
                }
        
        # 优先级3: 处理障碍物
        obstacle_cfg = config.get('obstacle', {})
        if obstacle_cfg.get('enabled', True):
            if self.sensor_snapshot.has_obstacle(obstacle_cfg.get('threshold_turn', 300)):
                action = self._get_obstacle_action()
                if action:
                    logger.info(f"[ExplorationManager] 检测到障碍物，执行避障: {action}")
                    return {
                        'type': 'obstacle_avoid',
                        'action': action,
                        'priority': 'normal'
                    }
        
        # 优先级4: 随机巡航
        return self._get_random_cruise_action()
    
    def _get_cliff_action(self) -> Optional[str]:
        """获取悬崖避障动作"""
        config = self.get_exploration_config()
        cliff_cfg = config.get('cliff', {})
        
        snapshot = self.sensor_snapshot
        
        if snapshot.cliff_fl:
            return 'front_left'
        elif snapshot.cliff_fr:
            return 'front_right'
        elif snapshot.cliff_bl:
            return 'back_left'
        elif snapshot.cliff_br:
            return 'back_right'
        
        return None
    
    def _get_obstacle_action(self) -> Optional[str]:
        """获取障碍物避障动作"""
        snapshot = self.sensor_snapshot
        obstacle_cfg = self.get_exploration_config().get('obstacle', {})
        threshold_turn = obstacle_cfg.get('threshold_turn', 300)
        
        left_blocked = snapshot.tof_left and snapshot.tof_left < threshold_turn
        right_blocked = snapshot.tof_right and snapshot.tof_right < threshold_turn
        
        if left_blocked and right_blocked:
            return 'both_blocked'
        elif left_blocked:
            return 'left_blocked'
        elif right_blocked:
            return 'right_blocked'
        
        return None
    
    def _get_random_cruise_action(self) -> Dict[str, Any]:
        """获取随机巡航动作"""
        config = self.get_exploration_config()
        cruise_cfg = config.get('cruise', {})
        behavior_cfg = config.get('behavior', {})
        
        # 随机决定是否改变方向
        if behavior_cfg.get('random_exploration', {}).get('enabled', True):
            if random.random() < behavior_cfg.get('random_exploration', {}).get('change_direction_probability', 0.2):
                return {
                    'type': 'turn',
                    'angle': random.uniform(cruise_cfg.get('turn_angle_min', 45), 
                                          cruise_cfg.get('turn_angle_max', 120)),
                    'direction': random.choice(['left', 'right'])
                }
        
        # 随机决定是否停顿
        if random.random() < cruise_cfg.get('pause_probability', 0.3):
            return {
                'type': 'pause',
                'duration': random.uniform(cruise_cfg.get('pause_duration_min', 1.0),
                                          cruise_cfg.get('pause_duration_max', 3.0))
            }
        
        # 默认直线移动
        return {
            'type': 'move',
            'speed': cruise_cfg.get('default_speed', 0.5),
            'duration': random.uniform(cruise_cfg.get('move_duration_min', 2.0),
                                      cruise_cfg.get('move_duration_max', 5.0))
        }
    
    async def _execute_action(self, action: Dict[str, Any], cruise_cfg: Dict[str, Any]):
        """执行动作"""
        try:
            action_type = action.get('type')
            
            if action_type == 'move':
                speed = action.get('speed', cruise_cfg.get('default_speed', 0.2))
                duration = action.get('duration', 0.5)
                await self.drive.motor_forward(speed, duration)
                self.state = ExplorationState.MOVING
                self.stats['total_moves'] += 1
                
                if self.config.get('exploration', {}).get('debug', {}).get('log_decisions', True):
                    logger.info(f"[ExplorationManager] → 直线移动: 速度={speed:.1f}, 时长={duration:.1f}s")
            
            elif action_type == 'turn':
                angle = action.get('angle', 90)
                direction = action.get('direction', 'left')
                turn_speed = cruise_cfg.get('turn_speed', 0.3)
                
                if direction == 'left':
                    await self.drive.motor_turn_left(turn_speed, angle / 360)
                else:
                    await self.drive.motor_turn_right(turn_speed, angle / 360)
                
                self.state = ExplorationState.TURNING
                self.stats['total_turns'] += 1
                
                if self.config.get('exploration', {}).get('debug', {}).get('log_decisions', True):
                    logger.info(f"[ExplorationManager] → 转向: 方向={direction}, 角度={angle:.0f}°")
            
            elif action_type == 'pause':
                duration = action.get('duration', 1.0)
                await self.drive.stop()
                self.state = ExplorationState.PAUSING
                await asyncio.sleep(duration)
                
                if self.config.get('exploration', {}).get('debug', {}).get('log_decisions', True):
                    logger.info(f"[ExplorationManager] → 停顿: {duration:.1f}s")
            
            elif action_type == 'cliff_avoid':
                await self._execute_cliff_avoidance(action)
                self.stats['cliff_avoidances'] += 1
            
            elif action_type == 'obstacle_avoid':
                await self._execute_obstacle_avoidance(action)
                self.stats['obstacle_avoidances'] += 1
        
        except Exception as e:
            logger.error(f"[ExplorationManager] 执行动作失败: {e}", exc_info=True)
            await self.drive.stop()
    
    async def _execute_cliff_avoidance(self, action: Dict[str, Any]):
        """执行悬崖避障"""
        config = self.get_exploration_config()
        cliff_cfg = config.get('cliff', {})
        brake_time = cliff_cfg.get('brake_time', 0.2)
        
        # 立即刹车
        await self.drive.stop()
        await asyncio.sleep(brake_time)
        
        cliff_action = action.get('action', 'front_left')
        
        if cliff_action == 'multiple_detected':
            # 多个悬崖：后退+180度转向
            cfg = cliff_cfg.get('multiple_detected', {})
            reverse_dist = cfg.get('reverse_distance', 15.0)
            turn_angle = cfg.get('turn_angle', 180)
            
            reverse_speed = cliff_cfg.get('reverse_speed', 0.4)
            logger.info(f"[ExplorationManager] 执行多悬崖避障: 后退{reverse_dist}cm...")
            await self.drive.motor_backward(reverse_speed, reverse_dist / 20)  # 估算时长
            await asyncio.sleep(0.8)
            
            logger.info(f"[ExplorationManager] 执行多悬崖避障: 转向{turn_angle}°...")
            await self.drive.motor_turn_left(cliff_cfg.get('turn_speed', 0.4), turn_angle / 360)
            await asyncio.sleep(0.8)
            
            logger.info(f"[ExplorationManager] ✓ 多悬崖避障完成: 后退{reverse_dist}cm, 转向{turn_angle}°")
        else:
            # 单个悬崖 - 根据位置区分处理
            sensor_cfg = cliff_cfg.get(cliff_action, {})
            action_type = sensor_cfg.get('action', 'turn_left')
            turn_angle = sensor_cfg.get('turn_angle', 90)
            reverse_first = sensor_cfg.get('reverse_first', True)
            
            # 前悬崖 (front_left/front_right)：先后退
            # 后悬崖 (back_left/back_right)：先前进
            if cliff_action.startswith('front'):
                if reverse_first:
                    reverse_dist = cliff_cfg.get('reverse_distance', 10.0)
                    reverse_speed = cliff_cfg.get('reverse_speed', 0.4)
                    logger.info(f"[ExplorationManager] [{cliff_action}] 检测到前悬崖，后退{reverse_dist}cm...")
                    await self.drive.motor_backward(reverse_speed, reverse_dist / 20)
                    await asyncio.sleep(0.5)
                
                # 前左悬崖 → 左转, 前右悬崖 → 右转
                if 'left' in cliff_action:
                    logger.info(f"[ExplorationManager] [{cliff_action}] 左转{turn_angle}°...")
                    await self.drive.motor_turn_left(cliff_cfg.get('turn_speed', 0.4), turn_angle / 360)
                else:  # front_right
                    logger.info(f"[ExplorationManager] [{cliff_action}] 右转{turn_angle}°...")
                    await self.drive.motor_turn_right(cliff_cfg.get('turn_speed', 0.4), turn_angle / 360)
            
            elif cliff_action.startswith('back'):
                # 后悬崖检测时应该前进避开（因为传感器在后面）
                logger.info(f"[ExplorationManager] [{cliff_action}] 检测到后悬崖，前进避开...")
                forward_dist = cliff_cfg.get('reverse_distance', 10.0) / 2  # 后面避障距离减半
                forward_speed = cliff_cfg.get('reverse_speed', 0.4)
                await self.drive.motor_forward(forward_speed, forward_dist / 20)
                await asyncio.sleep(0.5)
                
                # 后左悬崖 → 左转, 后右悬崖 → 右转
                if 'left' in cliff_action:
                    logger.info(f"[ExplorationManager] [{cliff_action}] 左转{turn_angle}°...")
                    await self.drive.motor_turn_left(cliff_cfg.get('turn_speed', 0.4), turn_angle / 360)
                else:  # back_right
                    logger.info(f"[ExplorationManager] [{cliff_action}] 右转{turn_angle}°...")
                    await self.drive.motor_turn_right(cliff_cfg.get('turn_speed', 0.4), turn_angle / 360)
            
            await asyncio.sleep(0.5)
            logger.info(f"[ExplorationManager] ✓ 悬崖避障完成 [{cliff_action}]")
        
        # ★ 关键修复：避障完成后立即清除所有悬崖状态
        self._clear_cliff_state()
    
    async def _execute_obstacle_avoidance(self, action: Dict[str, Any]):
        """执行障碍物避障"""
        config = self.get_exploration_config()
        obstacle_cfg = config.get('obstacle', {})
        
        # 冷却检查
        if time.time() - self.last_obstacle_time < obstacle_cfg.get('response_cooldown', 1.0):
            return
        
        self.last_obstacle_time = time.time()
        
        # 立即减速
        await self.drive.motor_forward(0.1, 0.5)
        
        obstacle_action = action.get('action', 'left_blocked')
        cfg = obstacle_cfg.get(obstacle_action, {})
        action_type = cfg.get('action', 'turn_left')
        turn_angle = cfg.get('turn_angle', 60)
        
        await self.drive.stop()
        await asyncio.sleep(0.5)
        
        if 'turn_left' in action_type:
            await self.drive.motor_turn_left(0.3, turn_angle / 360)
        elif 'turn_right' in action_type:
            await self.drive.motor_turn_right(0.3, turn_angle / 360)
        elif 'reverse' in action_type:
            reverse_dist = cfg.get('reverse_distance', 15.0)
            await self.drive.motor_backward(0.4, reverse_dist / 20)
            await asyncio.sleep(0.5)
            await self.drive.motor_turn_left(0.3, turn_angle / 360)
        
        await asyncio.sleep(0.5)
        logger.info(f"[ExplorationManager] 执行障碍物避障 [{obstacle_action}]: {action_type}, 转向{turn_angle}°")
    
    # 事件处理回调
    async def _on_cliff_front_left(self, data):
        """前左悬崖事件"""
        self.sensor_snapshot.cliff_fl = True
        logger.debug("[ExplorationManager] 检测到前左悬崖")
    
    async def _on_cliff_front_right(self, data):
        """前右悬崖事件"""
        self.sensor_snapshot.cliff_fr = True
        logger.debug("[ExplorationManager] 检测到前右悬崖")
    
    async def _on_cliff_back_left(self, data):
        """后左悬崖事件"""
        self.sensor_snapshot.cliff_bl = True
        logger.debug("[ExplorationManager] 检测到后左悬崖")
    
    async def _on_cliff_back_right(self, data):
        """后右悬崖事件"""
        self.sensor_snapshot.cliff_br = True
        logger.debug("[ExplorationManager] 检测到后右悬崖")
    
    async def _on_tof_left(self, data):
        """左ToF事件"""
        if isinstance(data, dict):
            self.sensor_snapshot.tof_left = data.get('distance')
        else:
            self.sensor_snapshot.tof_left = data
    
    async def _on_tof_right(self, data):
        """右ToF事件"""
        if isinstance(data, dict):
            self.sensor_snapshot.tof_right = data.get('distance')
        else:
            self.sensor_snapshot.tof_right = data
    
    async def _on_battery(self, data):
        """电池事件"""
        if isinstance(data, dict):
            self.sensor_snapshot.battery = data.get('percent')
        else:
            self.sensor_snapshot.battery = data
    
    def _get_elapsed_time(self) -> float:
        """获取已运行时间"""
        if not self.stats['start_time']:
            return 0.0
        return time.time() - self.stats['start_time']
    
    def _log_stats(self):
        """记录统计信息"""
        elapsed = self._get_elapsed_time()
        logger.info(f"""
[ExplorationManager] 探索统计:
  - 总运动次数: {self.stats['total_moves']}
  - 总转向次数: {self.stats['total_turns']}
  - 悬崖避障次数: {self.stats['cliff_avoidances']}
  - 障碍物避障次数: {self.stats['obstacle_avoidances']}
  - 运行总耗时: {elapsed:.0f}s
        """)
    
    def get_status(self) -> Dict[str, Any]:
        """获取当前状态"""
        return {
            'running': self.running,
            'state': self.state.value,
            'elapsed_time': self._get_elapsed_time(),
            'sensor_snapshot': {
                'cliff_fl': self.sensor_snapshot.cliff_fl,
                'cliff_fr': self.sensor_snapshot.cliff_fr,
                'cliff_bl': self.sensor_snapshot.cliff_bl,
                'cliff_br': self.sensor_snapshot.cliff_br,
                'tof_left': self.sensor_snapshot.tof_left,
                'tof_right': self.sensor_snapshot.tof_right,
                'battery': self.sensor_snapshot.battery,
            },
            'stats': self.stats,
        }
    
    async def _handle_sensor_event(self, event: 'DolyEvent') -> None:
        """
        处理来自daemon转发的传感器事件
        
        这是探索管理器的主要传感器事件处理接口
        由daemon在EXPLORING状态时调用
        """
        if not self.running:
            return
        
        try:
            # 导入事件类型（在这里延迟导入以避免循环依赖）
            from modules.doly.event_bus import EventType
            
            # 获取传感器事件处理开关
            sensor_handling = self.config.get('exploration', {}).get('sensor_event_handling', {})
            
            if event.type == EventType.CLIFF_DETECTED:
                # 检查悬崖事件处理是否启用
                if sensor_handling.get('cliff_event_enabled', False):
                    logger.debug(f"[ExplorationManager] ------------ 悬崖 已启用！: {event.data}")
                    await self._handle_cliff_event(event)
                else:
                    logger.debug(f"[ExplorationManager] 悬崖事件处理已禁用，忽略: {event.data}")
            
            elif event.type == EventType.OBSTACLE_DETECTED:
                # 检查障碍物事件处理是否启用
                if sensor_handling.get('obstacle_event_enabled', False):
                    logger.debug(f"[ExplorationManager] ------------ 障碍物 已启用！: {event.data}")
                    await self._handle_obstacle_event(event)
                else:
                    logger.debug(f"[ExplorationManager] 障碍物事件处理已禁用，忽略: {event.data}")
            
            elif event.type == EventType.BATTERY_LOW:
                # 检查电池事件处理是否启用
                if sensor_handling.get('battery_event_enabled', True):
                    await self._handle_battery_event(event)
                else:
                    logger.debug(f"[ExplorationManager] 电池事件处理已禁用，忽略: {event.data}")
        
        except Exception as e:
            logger.error(f"[ExplorationManager] 处理传感器事件异常: {e}", exc_info=True)
    
    async def _handle_cliff_event(self, event: 'DolyEvent') -> None:
        """处理悬崖检测事件"""
        try:
            # 提取位置信息
            position = event.data.get('position', '')
            pin = event.data.get('pin', '')
            value = event.data.get('value')
            
            # 如果是低级pin.change格式
            if pin and not position:
                if value is False:  # 低电平表示检测到
                    position_map = {
                        'IRS_FL': 'fl', 'IRS_FR': 'fr',
                        'IRS_BL': 'bl', 'IRS_BR': 'br'
                    }
                    position = position_map.get(pin, '')
                else:
                    # 高电平表示未检测到，忽略
                    return
            
            if not position:
                return
            
            # 更新传感器快照
            cliff_map = {
                'fl': 'cliff_fl', 'fr': 'cliff_fr',
                'bl': 'cliff_bl', 'br': 'cliff_br'
            }
            
            if position in cliff_map:
                setattr(self.sensor_snapshot, cliff_map[position], True)
                self.stats['cliff_avoidances'] += 1
                logger.warning(f"[ExplorationManager] 🚨 检测到悬崖: {position}")
                
                # 执行紧急避障
                await self._execute_cliff_avoidance()
        
        except Exception as e:
            logger.error(f"[ExplorationManager] 处理悬崖事件异常: {e}")
    
    async def _handle_obstacle_event(self, event: 'DolyEvent') -> None:
        """处理障碍物检测事件"""
        try:
            side = event.data.get('side', '')
            distance = event.data.get('distance_mm')
            
            if side == 'left' and distance is not None:
                self.sensor_snapshot.tof_left = distance
            elif side == 'right' and distance is not None:
                self.sensor_snapshot.tof_right = distance
            
            # 如果距离太近，执行避障
            if distance and distance < 100:
                self.stats['obstacle_avoidances'] += 1
                logger.warning(f"[ExplorationManager] ⚠️ 检测到近距离障碍物: {side} {distance}mm")
                await self._execute_obstacle_avoidance()
        
        except Exception as e:
            logger.error(f"[ExplorationManager] 处理障碍物事件异常: {e}")
    
    async def _handle_battery_event(self, event: 'DolyEvent') -> None:
        """处理电池事件"""
        try:
            battery_percent = event.data.get('percent', event.data.get('value'))
            if battery_percent is not None:
                self.sensor_snapshot.battery = battery_percent
                
                # 如果电池过低，停止探索
                if battery_percent < 10:
                    logger.warning(f"[ExplorationManager] 🔋 电池危险: {battery_percent}%, 停止探索")
                    await self.stop()
        
        except Exception as e:
            logger.error(f"[ExplorationManager] 处理电池事件异常: {e}")
