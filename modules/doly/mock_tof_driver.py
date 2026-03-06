"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

#!/usr/bin/env python3
"""
模拟TOF驱动器

用于在没有真实硬件时提供模拟的TOF距离数据。
"""

import time
import random
from typing import Optional, Dict


class MockTofDriver:
    """模拟TOF驱动器"""
    
    def __init__(self):
        self.random = random.Random()
        self.random.seed(42)  # 固定种子用于可重复测试
        self._initialized = True
        
        # 模拟状态
        self._base_left = 250
        self._base_right = 280
        self._variation_range = 100
        self._gesture_simulation = False
        self._obstacle_simulation = False
        
        print("[MockTofDriver] 模拟TOF驱动初始化完成")
    
    def read_distance(self, side: str) -> Optional[int]:
        """读取单侧距离"""
        time.sleep(0.02 + self.random.uniform(0.01, 0.03))  # 模拟20-50ms延迟
        
        if side.lower() == 'left':
            base = self._base_left
        elif side.lower() == 'right':
            base = self._base_right
        else:
            return None
        
        # 添加随机变化
        variation = self.random.randint(-50, 50)
        distance = max(50, min(500, base + variation))
        
        return distance
    
    def read_both(self) -> Dict[str, any]:
        """读取双侧距离"""
        time.sleep(0.03 + self.random.uniform(0.01, 0.04))  # 模拟30-70ms延迟
        
        left = self.read_distance('left')
        right = self.read_distance('right')
        
        # 偶尔模拟接近事件（避障测试）
        if self.random.random() < 0.1:  # 10%概率
            if self.random.random() < 0.5:
                left = self.random.randint(80, 150)  # 警告距离
            else:
                right = self.random.randint(80, 150)  # 警告距离
        
        # 更少概率模拟紧急情况
        if self.random.random() < 0.02:  # 2%概率
            left = self.random.randint(50, 100)  # 紧急距离
            right = self.random.randint(50, 100)  # 紧急距离
        
        return {
            'left': left,
            'right': right,
            'timestamp': time.time(),
            'mock': True
        }
    
    def is_healthy(self) -> bool:
        """检查驱动是否健康"""
        return self._initialized
    
    def set_simulation_mode(self, obstacle: bool = False, gesture: bool = False):
        """设置模拟模式"""
        self._obstacle_simulation = obstacle
        self._gesture_simulation = gesture
        
        if obstacle:
            self._base_left = 120
            self._base_right = 130
        elif gesture:
            self._base_left = 200
            self._base_right = 220
        else:
            self._base_left = 250
            self._base_right = 280