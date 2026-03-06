"""
眼神跟踪器（Gaze Tracker）

负责将人脸边界框坐标转换为 eyeEngine 的眼神位置坐标

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import time
import logging
from typing import Dict, Any, List, Tuple, Optional

logger = logging.getLogger(__name__)


class GazeTracker:
    """
    人脸位置 → 眼神坐标转换器
    
    功能：
    - 将 bbox 坐标转换为归一化的眼神坐标 (-1.0 ~ 1.0)
    - 应用平滑算法（避免抖动）
    - 应用死区机制（避免微小变化）
    - 限制坐标范围（避免眼球过度偏移）
    """
    
    def __init__(self, config: Optional[Dict[str, Any]] = None):
        """
        初始化眼神跟踪器
        
        Args:
            config: 配置字典
        """
        config = config or {}
        
        # 屏幕参数（eyeEngine LCD 分辨率）
        self.screen_width = config.get('screen_width', 480)
        self.screen_height = config.get('screen_height', 480)
        
        # 平滑参数
        self.smoothing = config.get('smoothing', 0.3)  # 0=无平滑，1=完全平滑
        
        # 死区阈值（小于此值不更新，避免抖动）
        self.dead_zone = config.get('dead_zone', 0.1)
        
        # 坐标范围限制
        self.max_x = config.get('max_x', 0.8)
        self.max_y = config.get('max_y', 0.6)
        
        # 更新间隔（秒）
        self.update_interval = config.get('update_interval', 0.2)
        
        # 状态
        self.last_gaze: Tuple[float, float] = (0.0, 0.0)  # 上次的眼神坐标
        self.last_update_time: float = 0.0  # 上次更新时间
        
        # 统计
        self.update_count = 0
        self.skip_count = 0
        
        logger.info(f"[GazeTracker] 初始化完成: screen={self.screen_width}x{self.screen_height}, "
                   f"smoothing={self.smoothing}, dead_zone={self.dead_zone}")
    
    def bbox_to_gaze(self, bbox: List[float]) -> Optional[Tuple[float, float]]:
        """
        将边界框转换为眼神坐标
        
        Args:
            bbox: [x, y, w, h] 边界框坐标
            
        Returns:
            (x, y) 归一化坐标，范围 (-1.0 ~ 1.0)，或 None（如果需要跳过更新）
        """
        # 检查更新间隔
        current_time = time.time()
        if current_time - self.last_update_time < self.update_interval:
            self.skip_count += 1
            return None
        
        # 解析 bbox
        if len(bbox) != 4:
            logger.warning(f"[GazeTracker] 无效的 bbox 格式: {bbox}")
            return None
        
        x, y, w, h = bbox
        
        # 计算人脸中心坐标（在摄像头坐标系中）
        face_center_x = x + w / 2
        face_center_y = y + h / 2
        
        # 计算屏幕中心
        screen_center_x = self.screen_width / 2
        screen_center_y = self.screen_height / 2
        
        # 计算相对屏幕中心的偏移
        offset_x = face_center_x - screen_center_x
        offset_y = face_center_y - screen_center_y
        
        # 归一化到 (-1.0, 1.0) 范围
        norm_x = offset_x / (self.screen_width / 2)
        norm_y = -offset_y / (self.screen_height / 2)  # Y 轴反转（屏幕坐标向下为正，眼神坐标向上为正）
        
        # 应用平滑（指数加权移动平均）
        if self.smoothing > 0:
            smooth_x = self.last_gaze[0] * self.smoothing + norm_x * (1 - self.smoothing)
            smooth_y = self.last_gaze[1] * self.smoothing + norm_y * (1 - self.smoothing)
        else:
            smooth_x = norm_x
            smooth_y = norm_y
        
        # 检查死区（避免微小变化）
        dx = abs(smooth_x - self.last_gaze[0])
        dy = abs(smooth_y - self.last_gaze[1])
        if dx < self.dead_zone and dy < self.dead_zone:
            self.skip_count += 1
            return None
        
        # 限制范围
        final_x = max(-self.max_x, min(self.max_x, smooth_x))
        final_y = max(-self.max_y, min(self.max_y, smooth_y))
        
        # 更新状态
        self.last_gaze = (final_x, final_y)
        self.last_update_time = current_time
        self.update_count += 1
        
        logger.debug(f"[GazeTracker] bbox={bbox} → gaze=({final_x:.2f}, {final_y:.2f})")
        
        return (final_x, final_y)
    
    def reset(self) -> None:
        """重置眼神位置到中心"""
        self.last_gaze = (0.0, 0.0)
        self.last_update_time = 0.0
        logger.debug("[GazeTracker] 重置到中心位置")
    
    def get_stats(self) -> Dict[str, Any]:
        """获取统计信息"""
        total = self.update_count + self.skip_count
        return {
            'update_count': self.update_count,
            'skip_count': self.skip_count,
            'skip_rate': self.skip_count / total if total > 0 else 0,
            'last_gaze': self.last_gaze,
            'last_update_time': self.last_update_time
        }
