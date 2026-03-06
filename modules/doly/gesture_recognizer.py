"""
手势识别器 - 识别和合并多次按压

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""
import time
import logging

logger = logging.getLogger(__name__)


class GestureRecognizer:
    """手势识别器，负责识别和合并多次按压事件"""
    
    def __init__(self, history_window: float = 2.0):
        """
        Args:
            history_window: 事件历史窗口时长（秒）
        """
        self.gesture_history = {}  # pin -> [(timestamp, gesture_type)]
        self.history_window = history_window
    
    def recognize_multipress(self, pin: str, gesture_type: str) -> str:
        """
        识别多次按压
        
        Args:
            pin: 传感器pin名称（如 TOUCH_L, TOUCH_R）
            gesture_type: 手势类型（SINGLE, DOUBLE, LONG_PRESS）
        
        Returns:
            识别后的手势类型（可能转换为LONG_PRESS_X2/X3或MULTIPLE）
        """
        now = time.time()
        
        # 清理超时历史
        if pin in self.gesture_history:
            self.gesture_history[pin] = [
                (ts, gt) for ts, gt in self.gesture_history[pin]
                if now - ts < self.history_window
            ]
        else:
            self.gesture_history[pin] = []
        
        # 记录新事件
        self.gesture_history[pin].append((now, gesture_type))
        
        # 统计当前窗口内的长按次数
        if gesture_type == 'LONG_PRESS':
            long_press_count = sum(
                1 for _, gt in self.gesture_history[pin] 
                if gt == 'LONG_PRESS'
            )
            
            if long_press_count == 1:
                return 'LONG_PRESS'
            elif long_press_count == 2:
                logger.info(f"[Gesture] {pin} 检测到2次长按")
                return 'LONG_PRESS_X2'
            elif long_press_count >= 3:
                logger.info(f"[Gesture] {pin} 检测到3+次长按")
                return 'LONG_PRESS_X3'
        
        # 处理双击 -> double_tap
        elif gesture_type == 'DOUBLE':
            logger.info(f"[Gesture] {pin} 识别到双击")
            return 'double_tap'
        
        # 统计短时间内的单击次数（转换为multiple）
        elif gesture_type == 'SINGLE':
            single_count = sum(
                1 for ts, gt in self.gesture_history[pin]
                if gt == 'SINGLE' and now - ts < 1.0  # 1秒内
            )
            
            if single_count >= 3:  # 连续3次以上单击
                logger.info(f"[Gesture] {pin} 检测到连续{single_count}次单击 → MULTIPLE")
                # 清空历史避免重复触发
                self.gesture_history[pin] = [(now, 'MULTIPLE')]
                return 'multiple_taps'
            
            # 处理单击 -> single_tap
            return 'single_tap'
        
        return gesture_type
    
    def reset_history(self, pin: str = None):
        """
        重置手势历史
        
        Args:
            pin: 指定pin（None表示重置所有）
        """
        if pin:
            self.gesture_history[pin] = []
            logger.debug(f"[Gesture] 重置 {pin} 历史")
        else:
            self.gesture_history.clear()
            logger.debug("[Gesture] 重置所有手势历史")
