"""
animation_system 集成接口

实现 animation_system 的 IEyeInterface 接口

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
from typing import TYPE_CHECKING

if TYPE_CHECKING:
    from ..engine import EyeEngine

logger = logging.getLogger(__name__)


class AnimationSystemInterface:
    """
    animation_system 眼睛接口实现
    
    允许 animation_system 通过标准接口控制眼睛引擎
    """
    
    def __init__(self, engine: 'EyeEngine'):
        """
        初始化接口
        
        Args:
            engine: 眼睛引擎实例
        """
        self._engine = engine
        self._controller = engine.controller
        
    def set_eye_color(self, color: str) -> None:
        """
        设置眼睛颜色
        
        Args:
            color: 颜色名称 (blue, red, green, etc.)
        """
        try:
            self._controller.set_iris_color(color)
            logger.debug(f"AnimationInterface: 设置眼睛颜色 {color}")
        except Exception as e:
            logger.error(f"AnimationInterface: 设置颜色失败: {e}")
    
    def set_pupil_position(self, x: float, y: float) -> None:
        """
        设置瞳孔位置
        
        Args:
            x: X 偏移 (-1.0 ~ 1.0)
            y: Y 偏移 (-1.0 ~ 1.0)
        """
        self._controller.set_iris_position(x, y)
        logger.debug(f"AnimationInterface: 设置瞳孔位置 ({x}, {y})")
    
    def set_eye_size(self, size: float) -> None:
        """
        设置眼睛大小 (通过眼睑控制)
        
        Args:
            size: 大小比例 (0.0 ~ 1.0, 1.0=完全睁开)
        """
        # 映射 size 到眼睑 ID
        # size 1.0 -> lid_id 0 (完全睁开)
        # size 0.5 -> lid_id 4 (半闭)
        # size 0.0 -> lid_id 8 (闭眼)
        
        if size >= 0.9:
            lid_id = 0
        elif size >= 0.7:
            lid_id = 2
        elif size >= 0.5:
            lid_id = 4
        elif size >= 0.3:
            lid_id = 6
        else:
            lid_id = 8
        
        self._controller.set_lid(side_id=lid_id)
        logger.debug(f"AnimationInterface: 设置眼睛大小 {size} -> lid_id {lid_id}")
    
    def blink(self) -> None:
        """执行眨眼动作"""
        self._controller.blink()
        logger.debug("AnimationInterface: 眨眼")
    
    def set_expression(self, expression: str) -> None:
        """
        设置表情
        
        Args:
            expression: 表情名称 (happy, sad, angry, etc.)
        """
        self._controller.set_expression(expression)
        logger.debug(f"AnimationInterface: 设置表情 {expression}")
    
    def look_at(self, x: float, y: float) -> None:
        """
        让眼睛看向指定方向
        
        Args:
            x: 水平方向 (-1.0=左, 1.0=右)
            y: 垂直方向 (-1.0=上, 1.0=下)
        """
        self._controller.look_at(x, y)
        logger.debug(f"AnimationInterface: 看向 ({x}, {y})")
    
    def play_animation(self, name: str, loop: bool = False) -> None:
        """
        播放眼睛动画
        
        Args:
            name: 动画名称
            loop: 是否循环
        """
        self._engine.play_sequence(name, loop=loop)
        logger.debug(f"AnimationInterface: 播放动画 {name}")
    
    def stop_animation(self) -> None:
        """停止当前动画"""
        self._engine.stop_sequence()
        logger.debug("AnimationInterface: 停止动画")
    
    def reset(self) -> None:
        """重置眼睛状态"""
        self._controller.reset()
        logger.debug("AnimationInterface: 重置")
    
    def set_brightness(self, level: int) -> None:
        """
        设置亮度
        
        Args:
            level: 亮度 (0-100)
        """
        self._controller.set_brightness(level)
        logger.debug(f"AnimationInterface: 设置亮度 {level}")
