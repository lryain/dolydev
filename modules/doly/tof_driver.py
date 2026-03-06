"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

#!/usr/bin/env python3
"""
Doly ToF 驱动封装

提供按需读取 ToF 距离传感器的接口，用于减少持续轮询的 CPU 占用。

用法:
    tof = TofDriver()
    
    # 读取单侧距离
    dist_left = tof.read_distance("left")
    dist_right = tof.read_distance("right")
    
    # 读取两侧距离
    dist_both = tof.read_both()
    
    # 带超时的读取
    dist = tof.read_distance_timeout("left", timeout_ms=100)
"""

import sys
import os
import logging
import time
from pathlib import Path
from typing import Optional, Dict, Tuple

# 添加 libs/sensors 到路径以导入 tof_reader
libs_sensors_path = os.path.abspath(os.path.join(os.path.dirname(__file__), '../../libs/sensors'))
if libs_sensors_path not in sys.path:
    sys.path.append(libs_sensors_path)

try:
    from tof_reader import TofReader, ReadTimeoutError
except ImportError as e:
    print(f"警告: 无法导入 TofReader: {e}")
    print("TofDriver 将在运行时失败。请确保 libs/sensors 在 Python 路径中")

logger = logging.getLogger(__name__)


class TofDriver:
    """
    ToF 驱动封装类
    
    功能:
    - 按需读取 ToF 距离
    - 自动处理超时和错误
    - 缓存最后一次读取结果
    - 性能统计
    """
    
    def __init__(self, config_file: Optional[str] = None):
        """
        初始化 ToF 驱动
        
        Args:
            config_file: libs/sensors/config.yaml 路径（可选）
        """
        self.reader = None
        self.last_read_time = 0.0
        self.read_count = 0
        self.error_count = 0
        
        # 缓存
        self.cached_left = None
        self.cached_right = None
        self.cache_timestamp = 0.0
        self.cache_ttl_ms = 100  # 缓存 TTL: 100ms
        
        try:
            self.reader = TofReader(config_file)
            logger.info("[TofDriver] 初始化成功")
        except Exception as e:
            logger.error(f"[TofDriver] 初始化失败: {e}")
            self.reader = None
    
    def read_distance(self, side: str) -> Optional[int]:
        """
        读取单侧距离
        
        Args:
            side: "left" 或 "right"
        
        Returns:
            距离(mm) 或 None (如果读取失败)
        """
        if not self.reader:
            logger.warning("[TofDriver] 读取器未初始化")
            return None
        
        try:
            start_time = time.time()
            
            if side == "left":
                distance = self.reader.read_left()
            elif side == "right":
                distance = self.reader.read_right()
            else:
                logger.error(f"[TofDriver] 无效的侧面: {side}")
                return None
            
            elapsed_ms = (time.time() - start_time) * 1000
            self.read_count += 1
            self.last_read_time = time.time()
            
            # 记录性能统计
            if self.read_count % 100 == 0:
                logger.debug(f"[TofDriver] {side} 距离: {distance}mm (耗时{elapsed_ms:.1f}ms)")
            
            return distance
            
        except ReadTimeoutError as e:
            self.error_count += 1
            logger.warning(f"[TofDriver] 读取超时 ({side}): {e}")
            return None
        except Exception as e:
            self.error_count += 1
            logger.error(f"[TofDriver] 读取异常 ({side}): {e}")
            return None
    
    def read_both(self) -> Dict[str, Optional[int]]:
        """
        读取两侧距离
        
        Returns:
            {"left": distance_mm, "right": distance_mm} 或 None
        """
        if not self.reader:
            logger.warning("[TofDriver] 读取器未初始化")
            return None
        
        try:
            start_time = time.time()
            
            # 使用 reader 的 read() 方法一次性读取两侧
            data = self.reader.read()
            
            elapsed_ms = (time.time() - start_time) * 1000
            self.read_count += 1
            self.last_read_time = time.time()
            
            logger.debug(f"[TofDriver] 双侧读取: L={data.get('left')}mm, R={data.get('right')}mm (耗时{elapsed_ms:.1f}ms)")
            
            return {
                "left": data.get("left"),
                "right": data.get("right"),
                "timestamp": data.get("timestamp")
            }
            
        except Exception as e:
            self.error_count += 1
            logger.error(f"[TofDriver] 双侧读取失败: {e}")
            return None
    
    def read_light_both(self) -> Optional[Dict[str, float]]:
        """
        读取双侧光照数据
        
        Returns:
            {"left_lux": float, "right_lux": float, "avg_lux": float, "timestamp": float}
        """
        if not self.reader:
            logger.warning("[TofDriver] 读取器未初始化（无法读取光照）")
            return None
        
        try:
            start_time = time.time()
            
            # 检查reader是否支持光照读取
            if not hasattr(self.reader, 'read_light_both'):
                logger.warning("[TofDriver] TofReader 不支持光照读取")
                return None
            
            light_data = self.reader.read_light_both()
            elapsed_ms = (time.time() - start_time) * 1000
            
            if light_data:
                logger.debug(f"[TofDriver] 光照读取: L={light_data['left_lux']:.1f}Lux, "
                           f"R={light_data['right_lux']:.1f}Lux, "
                           f"Avg={light_data['avg_lux']:.1f}Lux (耗时{elapsed_ms:.1f}ms)")
            
            return light_data
            
        except Exception as e:
            self.error_count += 1
            logger.error(f"[TofDriver] 光照读取失败: {e}")
            return None
    
    def set_light_gain(self, gain: int):
        """
        设置光照增益
        
        Args:
            gain: 增益值 (1, 2, 3, 4, 5, 10, 20, 40)
        """
        if not self.reader:
            logger.warning("[TofDriver] 读取器未初始化")
            return
        
        try:
            if hasattr(self.reader, 'set_light_gain'):
                self.reader.set_light_gain(gain)
                logger.info(f"[TofDriver] 光照增益设置为: {gain}")
            else:
                logger.warning("[TofDriver] TofReader 不支持设置光照增益")
        except Exception as e:
            logger.error(f"[TofDriver] 设置光照增益失败: {e}")
    
    def read_distance_cached(self, side: str, cache_ttl_ms: int = 100) -> Optional[int]:
        """
        读取距离（带缓存）
        
        在 cache_ttl_ms 内重复调用会返回缓存结果，减少 I2C 访问
        
        Args:
            side: "left" 或 "right"
            cache_ttl_ms: 缓存有效期（毫秒）
        
        Returns:
            距离(mm) 或 None
        """
        now = time.time()
        cache_age_ms = (now - self.cache_timestamp) * 1000
        
        # 如果缓存未过期，返回缓存
        if cache_age_ms < cache_ttl_ms:
            if side == "left" and self.cached_left is not None:
                return self.cached_left
            elif side == "right" and self.cached_right is not None:
                return self.cached_right
        
        # 缓存已过期或不存在，进行新读取
        distance = self.read_distance(side)
        
        # 更新缓存
        if distance is not None:
            if side == "left":
                self.cached_left = distance
            elif side == "right":
                self.cached_right = distance
            self.cache_timestamp = now
        
        return distance
    
    def get_statistics(self) -> Dict[str, any]:
        """
        获取统计信息
        
        Returns:
            统计字典
        """
        uptime_s = time.time() - self.last_read_time if self.last_read_time > 0 else 0
        
        return {
            "read_count": self.read_count,
            "error_count": self.error_count,
            "error_rate": self.error_count / max(1, self.read_count),
            "last_read_time": self.last_read_time,
            "uptime_s": uptime_s,
            "cached_left": self.cached_left,
            "cached_right": self.cached_right,
            "initialized": self.reader is not None
        }
    
    def is_healthy(self) -> bool:
        """
        检查驱动是否健康
        
        Returns:
            是否初始化且错误率低
        """
        if not self.reader:
            return False
        
        error_rate = self.error_count / max(1, self.read_count)
        return error_rate < 0.1  # 容许 10% 的错误率


# 测试代码
if __name__ == "__main__":
    logging.basicConfig(level=logging.DEBUG)
    
    print("=" * 60)
    print("ToF 驱动测试")
    print("=" * 60)
    
    # 初始化驱动
    tof = TofDriver()
    
    if not tof.is_healthy():
        print("✗ ToF 驱动初始化失败!")
        sys.exit(1)
    
    print("✓ ToF 驱动初始化成功\n")
    
    # 测试 1: 单侧读取
    print("📍 测试1: 单侧读取 (10次)")
    for i in range(10):
        dist_left = tof.read_distance("left")
        dist_right = tof.read_distance("right")
        print(f"  [{i+1:2d}] L={dist_left:4} mm, R={dist_right:4} mm")
        time.sleep(0.1)
    
    # 测试 2: 双侧读取
    print("\n📍 测试2: 双侧读取 (5次)")
    for i in range(5):
        data = tof.read_both()
        if data:
            print(f"  [{i+1}] L={data['left']:4} mm, R={data['right']:4} mm")
        time.sleep(0.1)
    
    # 测试 3: 缓存读取
    print("\n📍 测试3: 缓存读取 (100ms TTL)")
    dist1 = tof.read_distance_cached("left", cache_ttl_ms=100)
    print(f"  第1次读取: {dist1} mm")
    for i in range(5):
        dist = tof.read_distance_cached("left", cache_ttl_ms=100)
        status = "✓ 缓存" if dist == dist1 else "✗ 新读取"
        print(f"  第{i+2}次读取: {dist} mm ({status})")
        time.sleep(0.02)
    
    # 统计
    print("\n📊 统计信息:")
    stats = tof.get_statistics()
    for key, value in stats.items():
        if isinstance(value, float):
            print(f"  {key}: {value:.2f}")
        else:
            print(f"  {key}: {value}")
    
    print("\n✅ 测试完成!")
