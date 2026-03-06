"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

#!/usr/bin/env python3
"""
TOF数据提供器

负责异步获取TOF距离数据，提供智能缓存和事件检测功能。
设计用于集成到doly daemon中，支持避障和手势识别。
"""

import asyncio
import threading
import time
import logging
from collections import deque
from typing import Optional, Dict, List, Callable
from dataclasses import dataclass
import sys
import os

# 自动添加 libs/sensors 到 sys.path
libs_sensors_path = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../libs/sensors'))
if libs_sensors_path not in sys.path:
    sys.path.append(libs_sensors_path)

logger = logging.getLogger(__name__)


@dataclass
class TofDataPoint:
    """TOF数据点（含距离和光照）"""
    left_mm: Optional[int]
    right_mm: Optional[int]
    timestamp: float
    valid: bool = True
    read_latency_ms: float = 0.0
    # 光照数据（新增）
    left_lux: Optional[float] = None
    right_lux: Optional[float] = None
    avg_lux: Optional[float] = None
    brightness_level: str = "unknown"


class TofDataCache:
    """TOF数据智能缓存（含距离和光照）"""
    
    def __init__(self, ttl_ms: int = 50, light_ttl_ms: int = 1000):
        self.ttl_ms = ttl_ms
        self.light_ttl_ms = light_ttl_ms  # 光照变化慢，缓存时间更长
        self._lock = threading.Lock()
        self._data = {
            'left': {'value': None, 'timestamp': 0, 'valid': False},
            'right': {'value': None, 'timestamp': 0, 'valid': False},
            'both': {'value': None, 'timestamp': 0, 'valid': False},
            # 光照数据缓存（新增）
            'light': {'value': None, 'timestamp': 0, 'valid': False}
        }
    
    def get_single(self, side: str) -> Optional[int]:
        """获取单侧缓存数据"""
        with self._lock:
            entry = self._data.get(side)
            if entry and self._is_valid_entry(entry):
                return entry['value']
            return None
    
    def get_both(self) -> Optional[Dict[str, int]]:
        """获取双侧缓存数据"""
        with self._lock:
            entry = self._data.get('both')
            if entry and self._is_valid_entry(entry):
                return entry['value']
            return None
    
    def get_light(self) -> Optional[Dict[str, float]]:
        """获取光照缓存数据"""
        with self._lock:
            entry = self._data.get('light')
            if entry and self._is_valid_entry(entry, use_light_ttl=True):
                return entry['value']
            return None
    
    def set_single(self, side: str, value: Optional[int]):
        """设置单侧数据"""
        with self._lock:
            self._data[side] = {
                'value': value,
                'timestamp': time.time() * 1000,
                'valid': value is not None
            }
    
    def set_both(self, left: Optional[int], right: Optional[int]):
        """设置双侧数据"""
        with self._lock:
            now = time.time() * 1000
            
            # 更新单侧缓存
            self._data['left'] = {
                'value': left,
                'timestamp': now,
                'valid': left is not None
            }
            self._data['right'] = {
                'value': right,
                'timestamp': now,
                'valid': right is not None
            }
            
            # 更新双侧缓存
            if left is not None and right is not None:
                self._data['both'] = {
                    'value': {'left': left, 'right': right},
                    'timestamp': now,
                    'valid': True
                }
            else:
                self._data['both']['valid'] = False
    
    def set_light(self, light_data: dict):
        """设置光照数据
        
        Args:
            light_data: {'left_lux': float, 'right_lux': float, 'avg_lux': float, ...}
        """
        with self._lock:
            self._data['light'] = {
                'value': light_data,
                'timestamp': time.time() * 1000,
                'valid': light_data is not None
            }
    
    def _is_valid_entry(self, entry: dict, use_light_ttl: bool = False) -> bool:
        """检查缓存条目是否有效"""
        if not entry['valid']:
            return False
        age_ms = time.time() * 1000 - entry['timestamp']
        ttl = self.light_ttl_ms if use_light_ttl else self.ttl_ms
        return age_ms <= ttl
    
    def get_stats(self) -> Dict[str, any]:
        """获取缓存统计"""
        with self._lock:
            return {
                'ttl_ms': self.ttl_ms,
                'light_ttl_ms': self.light_ttl_ms,
                'left_valid': self._is_valid_entry(self._data['left']),
                'right_valid': self._is_valid_entry(self._data['right']),
                'both_valid': self._is_valid_entry(self._data['both']),
                'light_valid': self._is_valid_entry(self._data['light'], use_light_ttl=True),
                'left_age_ms': time.time() * 1000 - self._data['left']['timestamp'],
                'right_age_ms': time.time() * 1000 - self._data['right']['timestamp'],
                'light_age_ms': time.time() * 1000 - self._data['light']['timestamp']
            }


class TofEventDetector:
    """TOF事件检测器"""
    
    def __init__(self, config: dict):
        self.config = config
        self.obstacle_config = config.get('obstacle_detection', {})
        self.gesture_config = config.get('gesture_recognition', {})
        
        # 避障阈值
        self.emergency_distance = self.obstacle_config.get('emergency_distance', 30)
        self.warning_distance = self.obstacle_config.get('warning_distance', 150)
        
        # 手势检测参数
        self.gesture_window_size = self.gesture_config.get('window_size', 8)
        self.min_gesture_change = self.gesture_config.get('min_change', 50)
        
        # 历史数据
        self.distance_history = deque(maxlen=self.gesture_window_size)
        
        # 冷却机制 - 每种手势独立冷却
        self.last_obstacle_event = 0
        self.last_gesture_events = {
            'wave_left': 0,
            'wave_right': 0,
            'approach_both': 0,
            'slow_approach': 0,
            'slow_retreat': 0,
            'removed': 0  # 手移开事件
        }
        self.obstacle_cooldown = self.obstacle_config.get('cooldown_ms', 1000)
        self.gesture_cooldown = self.gesture_config.get('cooldown_ms', 3000)
        
        # 手势类型特定配置（含启用/禁用标志）
        gesture_types = self.gesture_config.get('gesture_types', {})
        self.wave_enabled = gesture_types.get('wave', {}).get('enabled', True)
        self.wave_cooldown = gesture_types.get('wave', {}).get('cooldown_ms', 2000)
        
        self.approach_enabled = gesture_types.get('approach', {}).get('enabled', True)
        self.approach_cooldown = gesture_types.get('approach', {}).get('cooldown_ms', 2000)
        
        self.slow_approach_enabled = gesture_types.get('slow_approach', {}).get('enabled', False)
        self.slow_approach_cooldown = gesture_types.get('slow_approach', {}).get('cooldown_ms', 3000)
        
        self.slow_retreat_enabled = gesture_types.get('slow_retreat', {}).get('enabled', False)
        self.slow_retreat_cooldown = gesture_types.get('slow_retreat', {}).get('cooldown_ms', 3000)
        
        # gesture_removed 配置
        removed_config = gesture_types.get('removed', {})
        self.removed_enabled = removed_config.get('enabled', True)
        self.removed_cooldown = removed_config.get('cooldown_ms', 2000)
        self.removed_threshold = removed_config.get('distance_threshold', 250)  # 超过此距离认为手移开
        self.removed_duration_ms = removed_config.get('min_duration_ms', 1000)  # 持续时间
        
        # 手在前方状态跟踪
        self.hand_present = False
        self.hand_absent_since = 0  # 手开始远离的时间
        
        logger.info(f"[TofEventDetector] 初始化完成: 紧急距离={self.emergency_distance}mm, 警告距离={self.warning_distance}mm")
        logger.info(f"[TofEventDetector] 手势启用状态: wave={self.wave_enabled}, approach={self.approach_enabled}, "
                   f"slow_approach={self.slow_approach_enabled}, slow_retreat={self.slow_retreat_enabled}, removed={self.removed_enabled}")
        logger.info(f"[TofEventDetector] 手势冷却时间: wave={self.wave_cooldown}ms, approach={self.approach_cooldown}ms, "
                   f"removed={self.removed_cooldown}ms")
    
    def detect_obstacle_events(self, left_mm: Optional[int], right_mm: Optional[int]) -> List[str]:
        """检测避障事件"""
        if not self.obstacle_config.get('enabled', True):
            return []
        
        events = []
        now = time.time() * 1000
        
        # 检查冷却时间
        if now - self.last_obstacle_event < self.obstacle_cooldown:
            return []
        
        # 检测紧急情况
        emergency_detected = False
        if left_mm is not None and left_mm <= self.emergency_distance:
            events.append('obstacle_emergency_left')
            emergency_detected = True
        
        if right_mm is not None and right_mm <= self.emergency_distance:
            events.append('obstacle_emergency_right')
            emergency_detected = True
        
        if left_mm is not None and right_mm is not None:
            if left_mm <= self.emergency_distance and right_mm <= self.emergency_distance:
                events.append('obstacle_emergency_both')
                emergency_detected = True
        
        # 检测警告情况
        if not emergency_detected:
            if left_mm is not None and left_mm <= self.warning_distance:
                events.append('obstacle_warning_left')
            
            if right_mm is not None and right_mm <= self.warning_distance:
                events.append('obstacle_warning_right')
            
            if (left_mm is not None and right_mm is not None and 
                left_mm <= self.warning_distance and right_mm <= self.warning_distance):
                events.append('obstacle_warning_both')
        
        if events:
            self.last_obstacle_event = now
            # logger.info(f"[TofEventDetector] 检测到避障事件: {events}, L={left_mm}mm, R={right_mm}mm")
        
        return events
    
    def detect_gesture_events(self, left_mm: Optional[int], right_mm: Optional[int]) -> List[str]:
        """
        检测手势事件（手势互斥 + 优先级）
        
        手势优先级（从高到低）：
        1. gesture_removed (手移开) - 基础事件
        2. gesture_approach_both (双手接近)
        3. gesture_wave_left/right (挥手)
        4. gesture_slow_approach (缓慢接近) - 可禁用
        5. gesture_slow_retreat (缓慢远离) - 可禁用
        
        返回：同一时刻最多返回1个手势事件（互斥）
        """
        if not self.gesture_config.get('enabled', True):
            return []
        
        events = []
        now = time.time() * 1000
        
        # 更新历史数据
        if left_mm is not None and right_mm is not None:
            self.distance_history.append({
                'left': left_mm,
                'right': right_mm,
                'timestamp': now
            })
            
            # 更新手在前方状态（用于检测 gesture_removed）
            avg_distance = (left_mm + right_mm) / 2
            if avg_distance < self.removed_threshold:
                # 手在近距离
                self.hand_present = True
                self.hand_absent_since = 0
            else:
                # 手在远距离
                if self.hand_present and self.hand_absent_since == 0:
                    # 刚开始远离
                    self.hand_absent_since = now
        
        # 优先级1：检测手移开事件（gesture_removed）
        if self.removed_enabled and now - self.last_gesture_events['removed'] >= self.removed_cooldown:
            if self._detect_gesture_removed(now):
                events.append('gesture_removed')
                self.last_gesture_events['removed'] = now
                logger.info(f"[TofEventDetector] 检测到手移开事件")
                return events  # 手势互斥：只返回最高优先级
        
        # 需要足够的历史数据才能检测其他手势
        if len(self.distance_history) < 5:
            return []
        
        # 最近的数据点（用于不同类型手势检测）
        recent_5 = list(self.distance_history)[-5:]  # 最近5个数据点（约333ms @ 15Hz）
        recent_6 = list(self.distance_history)[-6:] if len(self.distance_history) >= 6 else recent_5
        
        left_values_5 = [d['left'] for d in recent_5]
        right_values_5 = [d['right'] for d in recent_5]
        left_values_6 = [d['left'] for d in recent_6]
        right_values_6 = [d['right'] for d in recent_6]
        
        # 优先级2：双手接近（最重要的手势）
        if self.approach_enabled and now - self.last_gesture_events['approach_both'] >= self.approach_cooldown:
            if self._detect_approach_pattern(left_values_5, right_values_5):
                events.append('gesture_approach_both')
                self.last_gesture_events['approach_both'] = now
                logger.info(f"[TofEventDetector] 检测到双手接近: L={left_values_5}, R={right_values_5}")
                return events  # 手势互斥
        
        # 优先级3：挥手
        if self.wave_enabled:
            if now - self.last_gesture_events['wave_left'] >= self.wave_cooldown:
                if self._detect_wave_pattern(left_values_5):
                    events.append('gesture_wave_left')
                    self.last_gesture_events['wave_left'] = now
                    logger.info(f"[TofEventDetector] 检测到左侧挥手: {left_values_5}")
                    return events  # 手势互斥
            
            if now - self.last_gesture_events['wave_right'] >= self.wave_cooldown:
                if self._detect_wave_pattern(right_values_5):
                    events.append('gesture_wave_right')
                    self.last_gesture_events['wave_right'] = now
                    logger.info(f"[TofEventDetector] 检测到右侧挥手: {right_values_5}")
                    return events  # 手势互斥
        
        # 优先级4：缓慢接近（可禁用）
        if self.slow_approach_enabled and now - self.last_gesture_events['slow_approach'] >= self.slow_approach_cooldown:
            if self._detect_slow_approach(left_values_6, right_values_6):
                events.append('gesture_slow_approach')
                self.last_gesture_events['slow_approach'] = now
                logger.info(f"[TofEventDetector] 检测到缓慢接近手势: L={left_values_6}, R={right_values_6}")
                return events  # 手势互斥
        
        # 优先级5：缓慢远离（可禁用）
        if self.slow_retreat_enabled and now - self.last_gesture_events['slow_retreat'] >= self.slow_retreat_cooldown:
            if self._detect_slow_retreat(left_values_6, right_values_6):
                events.append('gesture_slow_retreat')
                self.last_gesture_events['slow_retreat'] = now
                logger.info(f"[TofEventDetector] 检测到缓慢远离手势: L={left_values_6}, R={right_values_6}")
                return events  # 手势互斥
        
        return events  # 未检测到任何手势
    
    def _detect_gesture_removed(self, now: float) -> bool:
        """
        检测手移开事件
        
        条件：
        1. 之前手在前方（hand_present=True）
        2. 距离持续超过阈值（默认250mm）
        3. 持续时间超过最小时长（默认1000ms）
        """
        if not self.hand_present:
            return False  # 手本来就不在前方
        
        if self.hand_absent_since == 0:
            return False  # 手还没开始远离
        
        # 检查持续时间
        absent_duration = now - self.hand_absent_since
        if absent_duration >= self.removed_duration_ms:
            # 手已经移开足够长时间
            self.hand_present = False  # 重置状态
            self.hand_absent_since = 0
            return True
        
        return False
    
    def _detect_wave_pattern(self, values: List[int]) -> bool:
        """检测挥手模式：先减小后增大"""
        if len(values) < 5:
            return False
        
        # 找到最小值的位置
        min_idx = values.index(min(values))
        
        # 最小值应该在中间位置
        if min_idx < 1 or min_idx > len(values) - 2:
            return False
        
        # 检查前半部分是否递减
        decreasing = True
        for i in range(min_idx):
            if i > 0 and values[i] >= values[i-1]:
                decreasing = False
                break
        
        # 检查后半部分是否递增
        increasing = True
        for i in range(min_idx + 1, len(values)):
            if values[i] <= values[i-1]:
                increasing = False
                break
        
        # 检查变化幅度
        total_change = max(values) - min(values)
        
        return decreasing and increasing and total_change >= self.min_gesture_change
    
    def _detect_approach_pattern(self, left_values: List[int], right_values: List[int]) -> bool:
        """检测双手同时接近模式"""
        if len(left_values) < 3 or len(right_values) < 3:
            return False
        
        # 检查两侧是否都在减小
        left_decreasing = all(left_values[i] <= left_values[i-1] for i in range(1, len(left_values)))
        right_decreasing = all(right_values[i] <= right_values[i-1] for i in range(1, len(right_values)))
        
        # 检查最终距离是否足够近
        final_left = left_values[-1]
        final_right = right_values[-1]
        
        # 检查变化幅度
        left_change = left_values[0] - left_values[-1]
        right_change = right_values[0] - right_values[-1]
        
        return (left_decreasing and right_decreasing and 
                final_left <= 200 and final_right <= 200 and
                left_change >= self.min_gesture_change and 
                right_change >= self.min_gesture_change)
    
    def _detect_slow_approach(self, left_values: List[int], right_values: List[int]) -> bool:
        """
        检测缓慢接近手势（主人手缓慢靠近）→ Doly后退躲避
        
        检测规则：
        1. 双手都在近距离（<150mm）
        2. 距离在逐渐减少（手靠近）
        3. 持续至少400ms（约6帧）
        4. 总变化>=15mm
        """
        if len(left_values) < 6 or len(right_values) < 6:
            return False
        
        # 检查当前双手都在近距离（<150mm）
        current_left = left_values[-1]
        current_right = right_values[-1]
        
        if current_left >= 150 or current_right >= 150:
            return False  # 至少有一侧太远
        
        # 检查所有点都在近距离（<150mm）- 确保持续被挡
        all_left_close = all(v < 150 for v in left_values)
        all_right_close = all(v < 150 for v in right_values)
        
        if not (all_left_close and all_right_close):
            return False
        
        # 检查距离是否在减少（手靠近 = 距离变小）
        left_start = left_values[0]
        left_end = left_values[-1]
        right_start = right_values[0]
        right_end = right_values[-1]
        
        left_decreasing = left_start - left_end >= 15  # 减少至少15mm
        right_decreasing = right_start - right_end >= 15
        
        if not (left_decreasing and right_decreasing):
            return False
        
        # 检查趋势一致性（允许小抖动±5mm）
        left_consistent = sum(1 for i in range(len(left_values)-1) 
                            if left_values[i] >= left_values[i+1] - 5) >= 4
        right_consistent = sum(1 for i in range(len(right_values)-1) 
                             if right_values[i] >= right_values[i+1] - 5) >= 4
        
        return left_consistent and right_consistent
    
    def _detect_slow_retreat(self, left_values: List[int], right_values: List[int]) -> bool:
        """
        检测缓慢远离手势（主人手缓慢远离）→ Doly前进跟随
        
        检测规则：
        1. 起始时双手都在近距离（<150mm）
        2. 距离在逐渐增加（手远离）
        3. 持续至少400ms（约6帧）
        4. 总变化>=15mm
        """
        if len(left_values) < 6 or len(right_values) < 6:
            return False
        
        # 检查起始时双手都在近距离（<150mm）
        start_left = left_values[0]
        start_right = right_values[0]
        
        if start_left >= 150 or start_right >= 150:
            return False  # 起始距离太远
        
        # 检查所有点都在合理范围内（起始<150mm，结束可以>150mm但<200mm）
        all_left_valid = all(v < 200 for v in left_values) and left_values[0] < 150
        all_right_valid = all(v < 200 for v in right_values) and right_values[0] < 150
        
        if not (all_left_valid and all_right_valid):
            return False
        
        # 检查距离是否在增加（手远离 = 距离变大）
        left_start = left_values[0]
        left_end = left_values[-1]
        right_start = right_values[0]
        right_end = right_values[-1]
        
        left_increasing = left_end - left_start >= 15  # 增加至少15mm
        right_increasing = right_end - right_start >= 15
        
        if not (left_increasing and right_increasing):
            return False
        
        # 检查趋势一致性（允许小抖动±5mm）
        left_consistent = sum(1 for i in range(len(left_values)-1) 
                            if left_values[i] <= left_values[i+1] + 5) >= 4
        right_consistent = sum(1 for i in range(len(right_values)-1) 
                             if right_values[i] <= right_values[i+1] + 5) >= 4
        
        return left_consistent and right_consistent


class TofDataProvider:
    """TOF数据异步获取提供器"""
    
    def __init__(self, config: dict):
        self.config = config
        self.enabled = config.get('enabled', True)
        self.hardware_available = config.get('hardware_available', True)
        
        # 数据获取配置
        self.data_config = config.get('data_acquisition', {})
        self.background_frequency = self.data_config.get('background_read_frequency', 15)
        self.cache_ttl = self.data_config.get('cache_ttl_ms', 50)
        self.max_errors = self.data_config.get('max_read_errors', 10)
        
        # 组件初始化
        self.cache = TofDataCache(self.cache_ttl)
        self.event_detector = TofEventDetector(config)
        
        # 硬件接口
        self.tof_driver = None
        self.using_mock = False
        
        # 后台线程管理
        self.reader_thread = None
        self.reader_stop_event = threading.Event()
        self.reader_running = False
        
        # 事件回调
        self.event_callbacks: List[Callable] = []
        
        # 统计信息
        self.stats = {
            'total_reads': 0,
            'successful_reads': 0,
            'cache_hits': 0,
            'events_detected': 0,
            'consecutive_errors': 0,
            'last_read_time': 0,
            'last_event_time': 0
        }
        
        logger.info(f"[TofDataProvider] 初始化: enabled={self.enabled}, frequency={self.background_frequency}Hz")
    
    async def initialize(self):
        """初始化TOF驱动"""
        if not self.enabled:
            logger.info("[TofDataProvider] TOF集成已禁用")
            return True
        
        try:
            # 绝对导入，兼容直接运行
            from modules.doly.tof_driver import TofDriver
            self.tof_driver = TofDriver()
            if self.tof_driver.is_healthy():
                logger.info("[TofDataProvider] 真实TOF硬件初始化成功")
                self.hardware_available = True
            else:
                raise Exception("TOF硬件健康检查失败")
        except Exception as e:
            logger.warning(f"[TofDataProvider] 无法初始化真实硬件: {e}")
            try:
                from modules.doly.mock_tof_driver import MockTofDriver
                self.tof_driver = MockTofDriver()
                self.using_mock = True
                self.hardware_available = False
                logger.info("[TofDataProvider] 使用模拟TOF驱动")
            except Exception as e2:
                logger.error(f"[TofDataProvider] 模拟驱动导入失败: {e2}")
                self.tof_driver = None
        return True
    
    def start_background_reader(self):
        """启动后台读取线程"""
        if not self.enabled or self.reader_running:
            return
        
        self.reader_stop_event.clear()
        self.reader_thread = threading.Thread(target=self._background_reader_loop, daemon=True)
        self.reader_thread.start()
        self.reader_running = True
        logger.info(f"[TofDataProvider] 后台读取线程已启动，频率: {self.background_frequency}Hz")
    
    def stop_background_reader(self):
        """停止后台读取线程"""
        if not self.reader_running:
            return
        
        self.reader_stop_event.set()
        if self.reader_thread and self.reader_thread.is_alive():
            self.reader_thread.join(timeout=2.0)
        
        self.reader_running = False
        logger.info("[TofDataProvider] 后台读取线程已停止")
    
    def _background_reader_loop(self):
        """后台读取循环"""
        interval = 1.0 / self.background_frequency
        
        logger.info(f"[TofDataProvider] 后台读取循环开始，间隔: {interval:.3f}s")
        
        while not self.reader_stop_event.wait(interval):
            try:
                self._perform_background_read()
            except Exception as e:
                logger.error(f"[TofDataProvider] 后台读取异常: {e}")
                self.stats['consecutive_errors'] += 1
                
                # 错误过多时降频
                if self.stats['consecutive_errors'] > self.max_errors:
                    interval = min(interval * 1.5, 1.0)  # 最多降到1Hz
                    logger.warning(f"[TofDataProvider] 连续错误过多，降频至{1/interval:.1f}Hz")
        
        logger.info("[TofDataProvider] 后台读取循环结束")
    
    def _perform_background_read(self):
        """执行后台读取"""
        start_time = time.time()
        
        try:
            # 读取双侧数据
            data = self.tof_driver.read_both()
            
            if data:
                left_mm = data.get('left')
                right_mm = data.get('right')
                
                # 更新缓存
                self.cache.set_both(left_mm, right_mm)
                
                # 检测事件
                events = []
                events.extend(self.event_detector.detect_obstacle_events(left_mm, right_mm))
                events.extend(self.event_detector.detect_gesture_events(left_mm, right_mm))
                
                # 触发事件回调
                if events:
                    self._trigger_event_callbacks(events, {'left': left_mm, 'right': right_mm})
                    self.stats['events_detected'] += len(events)
                    self.stats['last_event_time'] = time.time()
                
                # 更新统计
                self.stats['successful_reads'] += 1
                self.stats['consecutive_errors'] = 0
                
            else:
                self.stats['consecutive_errors'] += 1
                
        except Exception as e:
            logger.error(f"[TofDataProvider] 读取失败: {e}")
            self.stats['consecutive_errors'] += 1
        
        finally:
            self.stats['total_reads'] += 1
            self.stats['last_read_time'] = time.time()
    
    async def get_distance(self, side: str) -> Optional[int]:
        """获取单侧距离（优先缓存）"""
        if not self.enabled:
            return None
        
        # 尝试从缓存获取
        cached_value = self.cache.get_single(side)
        if cached_value is not None:
            self.stats['cache_hits'] += 1
            return cached_value
        
        # 缓存未命中，直接读取硬件
        try:
            distance = self.tof_driver.read_distance(side)
            if distance is not None:
                self.cache.set_single(side, distance)
            return distance
        except Exception as e:
            logger.error(f"[TofDataProvider] 直接读取{side}侧失败: {e}")
            return None
    
    async def get_both_distances(self) -> Optional[Dict[str, int]]:
        """获取双侧距离（优先缓存）"""
        if not self.enabled:
            return None
        
        # 尝试从缓存获取
        cached_data = self.cache.get_both()
        if cached_data is not None:
            self.stats['cache_hits'] += 1
            return cached_data
        
        # 缓存未命中，直接读取硬件
        try:
            data = self.tof_driver.read_both()
            if data and data.get('left') is not None and data.get('right') is not None:
                result = {'left': data['left'], 'right': data['right']}
                self.cache.set_both(data['left'], data['right'])
                return result
            return None
        except Exception as e:
            logger.error(f"[TofDataProvider] 直接读取双侧失败: {e}")
            return None
    
    def register_event_callback(self, callback: Callable[[List[str], Dict], None]):
        """注册事件回调"""
        self.event_callbacks.append(callback)
        logger.debug(f"[TofDataProvider] 注册事件回调，总数: {len(self.event_callbacks)}")
    
    def _trigger_event_callbacks(self, events: List[str], data: Dict):
        """触发事件回调"""
        for callback in self.event_callbacks:
            try:
                callback(events, data)
            except Exception as e:
                logger.error(f"[TofDataProvider] 事件回调执行失败: {e}")
    
    def get_statistics(self) -> Dict[str, any]:
        """获取统计信息"""
        cache_stats = self.cache.get_stats()
        
        return {
            'enabled': self.enabled,
            'hardware_available': self.hardware_available,
            'using_mock': self.using_mock,
            'reader_running': self.reader_running,
            'background_frequency': self.background_frequency,
            **self.stats,
            'cache_stats': cache_stats,
            'success_rate': self.stats['successful_reads'] / max(1, self.stats['total_reads']),
            'cache_hit_rate': self.stats['cache_hits'] / max(1, self.stats['total_reads']),
        }
    
    async def get_light_data(self) -> Optional[Dict[str, float]]:
        """获取光照数据（从缓存，1秒TTL）
        
        Returns:
            dict: {'left_lux': float, 'right_lux': float, 'avg_lux': float, 'brightness_level': str}
        """
        if not self.enabled:
            return None
        
        # 尝试从缓存获取
        cached_light = self.cache.get_light()
        if cached_light is not None:
            return cached_light
        
        # 缓存未命中，直接读取硬件
        try:
            if hasattr(self.tof_driver, 'read_light_both'):
                light_data = self.tof_driver.read_light_both()
                if light_data:
                    # 分类亮度级别
                    brightness = self._classify_brightness(light_data.get('avg_lux'))
                    result = {
                        **light_data,
                        'brightness_level': brightness
                    }
                    self.cache.set_light(result)
                    return result
        except Exception as e:
            logger.error(f"[TofDataProvider] 读取光照数据失败: {e}")
        
        return None
    
    def _classify_brightness(self, lux: Optional[float]) -> str:
        """根据Lux值分类亮度级别
        
        Args:
            lux: 光照值（单位：Lux）
            
        Returns:
            亮度级别字符串
        """
        if lux is None:
            return "unknown"
        
        # 从配置读取阈值，如果没有使用默认值
        ambient_light_config = self.config.get('ambient_light', {})
        thresholds = ambient_light_config.get('brightness_thresholds', {
            'very_dark': 10,
            'dark': 50,
            'normal': 200,
            'bright': 500,
            'very_bright': 1000
        })
        
        if lux < thresholds.get('very_dark', 10):
            return "very_dark"
        elif lux < thresholds.get('dark', 50):
            return "dark"
        elif lux < thresholds.get('normal', 200):
            return "normal"
        elif lux < thresholds.get('bright', 500):
            return "bright"
        else:
            return "very_bright"
    
    def is_healthy(self) -> bool:
        """检查提供器是否健康"""
        return (self.enabled and 
                self.stats['consecutive_errors'] < self.max_errors and
                (self.using_mock or self.hardware_available))