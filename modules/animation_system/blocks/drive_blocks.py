"""
驱动控制块

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from typing import Any, Dict, Optional, List, TYPE_CHECKING
import logging

from .base_block import BaseBlock
from ..hardware_interfaces import HardwareInterfaces

if TYPE_CHECKING:
    from . import AnimationBlock

logger = logging.getLogger(__name__)


class DriveDistanceBlock(BaseBlock):
    """直线移动块 (基于真实距离)"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('drive_distance', fields, statements)
        self.distance = self.get_field('distance', 0)  # 距离（厘米）
        self.speed = self.get_field('speed', 20)  # 速度百分比（0-100）
        self.accel = self.get_field('accel', 0)  # 加速度（暂不支持）
        self.brake = self.get_field('brake', 0)  # 刹车（暂不支持）
        self.direction = self.get_field('direction', 0)  # 0=前进, 1=后退
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行直线移动"""
        if not interfaces.drive:
            logger.warning("[DriveDistanceBlock] Drive interface not available")
            return
        
        # 根据 direction 决定距离的正负号
        # direction: 0=前进(正数), 1=后退(负数)
        actual_distance = self.distance if self.direction == 0 else -self.distance
        
        direction_str = "前进" if self.direction == 0 else "后退"
        logger.info(f"[DriveDistanceBlock] 驱动 {direction_str}: {self.distance}cm, "
                   f"速度={self.speed}%, actual_distance={actual_distance}cm")
        
        try:
            # 使用新的 move_distance_cm 方法
            await interfaces.drive.move_distance_cm(actual_distance, self.speed)
            logger.debug(f"[DriveDistanceBlock] 移动完成")
        except Exception as e:
            logger.error(f"[DriveDistanceBlock] 移动失败: {e}")
            raise
    
    def validate(self) -> bool:
        """验证参数有效"""
        return self.distance >= 0 and self.speed > 0 and self.direction in [0, 1]


class DriveForwardBlock(BaseBlock):
    """电机前进块 (基于时长)"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('drive_forward', fields, statements)
        self.speed = self.get_field('speed', 20)  # 速度百分比
        self.duration = self.get_field('duration', 0)  # 持续时长（秒）
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行电机前进"""
        if not interfaces.drive:
            logger.warning("[DriveForwardBlock] Drive interface not available")
            return
        
        logger.info(f"[DriveForwardBlock] 电机前进: speed={self.speed}%, duration={self.duration}s")
        
        try:
            # 将百分比速度转换为0-1范围
            speed_normalized = self.speed / 100.0
            await interfaces.drive.motor_forward(speed_normalized, self.duration)
            logger.debug(f"[DriveForwardBlock] 前进完成")
        except Exception as e:
            logger.error(f"[DriveForwardBlock] 前进失败: {e}")
            raise
    
    def validate(self) -> bool:
        """验证参数有效"""
        return self.speed > 0 and self.duration >= 0


class DriveBackwardBlock(BaseBlock):
    """电机后退块 (基于时长)"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('drive_backward', fields, statements)
        self.speed = self.get_field('speed', 20)  # 速度百分比
        self.duration = self.get_field('duration', 0)  # 持续时长（秒）
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行电机后退"""
        if not interfaces.drive:
            logger.warning("[DriveBackwardBlock] Drive interface not available")
            return
        
        logger.info(f"[DriveBackwardBlock] 电机后退: speed={self.speed}%, duration={self.duration}s")
        
        try:
            # 将百分比速度转换为0-1范围
            speed_normalized = self.speed / 100.0
            await interfaces.drive.motor_backward(speed_normalized, self.duration)
            logger.debug(f"[DriveBackwardBlock] 后退完成")
        except Exception as e:
            logger.error(f"[DriveBackwardBlock] 后退失败: {e}")
            raise
    
    def validate(self) -> bool:
        """验证参数有效"""
        return self.speed > 0 and self.duration >= 0


class DriveRotateLeftBlock(BaseBlock):
    """左转块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('drive_rotate_left', fields, statements)
        self.angle = self.get_field('driveRotate', 0)
        self.is_center = self.get_field('isCenter', True)
        self.speed = self.get_field('speed', 1)
        self.accel = self.get_field('accel', 20)
        self.brake = self.get_field('brake', 0)
        self.duration = self.get_field('duration', 0)  # 新增：支持按时长左转（秒）
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行左转"""
        if not interfaces.drive:
            logger.warning("[DriveRotateLeftBlock] Drive interface not available")
            return
        
        # 如果指定了 duration（>0），则使用基于时长的转向
        if self.duration > 0:
            logger.info(f"[DriveRotateLeftBlock] 左转(duration模式): speed={self.speed}, duration={self.duration}s")
            try:
                # 如果 speed 是百分比（>1），则归一化
                speed_normalized = self.speed / 100.0 if self.speed > 1 else self.speed
                await interfaces.drive.motor_turn_left(speed_normalized, self.duration)
                logger.debug(f"[DriveRotateLeftBlock] 左转完成(duration)")
            except Exception as e:
                logger.error(f"[DriveRotateLeftBlock] 左转失败: {e}")
                raise
        else:
            # 否则使用角度模式
            rotate_type = "原地" if self.is_center else "移动"
            logger.info(f"[DriveRotateLeftBlock] 左转(angle模式): {self.angle}° ({rotate_type}旋转), "
                       f"速度={self.speed}, 加速度={self.accel}")
            
            try:
                await interfaces.drive.rotate_left(
                    self.angle,
                    self.is_center,
                    self.speed,
                    self.accel,
                    self.brake
                )
                logger.debug(f"[DriveRotateLeftBlock] 左转完成(angle)")
            except Exception as e:
                logger.error(f"[DriveRotateLeftBlock] 左转失败: {e}")
                raise
    
    def validate(self) -> bool:
        """验证参数有效"""
        if self.duration > 0:
            return self.speed > 0
        return self.angle >= 0 and self.speed > 0


class DriveRotateRightBlock(BaseBlock):
    """右转块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('drive_rotate_right', fields, statements)
        self.angle = self.get_field('driveRotate', 0)
        self.is_center = self.get_field('isCenter', True)
        self.speed = self.get_field('speed', 1)
        self.accel = self.get_field('accel', 20)
        self.brake = self.get_field('brake', 0)
        self.duration = self.get_field('duration', 0)  # 新增：支持按时长右转（秒）
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行右转"""
        if not interfaces.drive:
            logger.warning("[DriveRotateRightBlock] Drive interface not available")
            return
        
        # 如果指定了 duration（>0），则使用基于时长的转向
        if self.duration > 0:
            logger.info(f"[DriveRotateRightBlock] 右转(duration模式): speed={self.speed}, duration={self.duration}s")
            try:
                # 如果 speed 是百分比（>1），则归一化
                speed_normalized = self.speed / 100.0 if self.speed > 1 else self.speed
                await interfaces.drive.motor_turn_right(speed_normalized, self.duration)
                logger.debug(f"[DriveRotateRightBlock] 右转完成(duration)")
            except Exception as e:
                logger.error(f"[DriveRotateRightBlock] 右转失败: {e}")
                raise
        else:
            # 否则使用角度模式
            rotate_type = "原地" if self.is_center else "移动"
            logger.info(f"[DriveRotateRightBlock] 右转(angle模式): {self.angle}° ({rotate_type}旋转), "
                       f"速度={self.speed}, 加速度={self.accel}")
            
            try:
                await interfaces.drive.rotate_right(
                    self.angle,
                    self.is_center,
                    self.speed,
                    self.accel,
                    self.brake
                )
                logger.debug(f"[DriveRotateRightBlock] 右转完成(angle)")
            except Exception as e:
                logger.error(f"[DriveRotateRightBlock] 右转失败: {e}")
                raise
    
    def validate(self) -> bool:
        """验证参数有效"""
        if self.duration > 0:
            return self.speed > 0
        return self.angle >= 0 and self.speed > 0


class DriveSetAngleBlock(BaseBlock):
    """设置舵机角度块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('drive_set_angle', fields, statements)
        self.channel = self.get_field('channel', 'left')
        self.angle = self.get_field('angle', 90)
        self.speed = self.get_field('speed', 40)
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行舵机角度设置"""
        if not interfaces.drive:
            logger.warning("[DriveSetAngleBlock] Drive interface not available")
            return
        
        logger.info(f"[DriveSetAngleBlock] 设置 {self.channel} 舵机: {self.angle}°, 速度: {self.speed}")
        
        try:
            await interfaces.drive.set_servo_angle(self.channel, self.angle, self.speed)
        except Exception as e:
            logger.error(f"[DriveSetAngleBlock] 失败: {e}")
            raise
    
    def validate(self) -> bool:
        """验证参数有效"""
        return self.channel in ['left', 'right'] and 0 <= self.angle <= 180 and self.speed > 0


class DriveSwingBlock(BaseBlock):
    """舵机摆动块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('drive_swing', fields, statements)
        self.channel = self.get_field('channel', 'left')
        self.min_angle = self.get_field('min_angle', 45)
        self.max_angle = self.get_field('max_angle', 135)
        self.duration_ms = self.get_field('duration_ms', 400)
        self.count = self.get_field('count', 5)
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行舵机摆动"""
        if not interfaces.drive:
            logger.warning("[DriveSwingBlock] Drive interface not available")
            return
        
        logger.info(f"[DriveSwingBlock] {self.channel} 摆动 {self.count} 次 "
                   f"({self.min_angle}°-{self.max_angle}°)")
        
        try:
            await interfaces.drive.servo_swing(
                self.channel, self.min_angle, self.max_angle, 
                self.duration_ms, self.count
            )
        except Exception as e:
            logger.error(f"[DriveSwingBlock] 失败: {e}")
            raise
    
    def validate(self) -> bool:
        """验证参数有效"""
        return (0 <= self.min_angle <= 180 and 
                0 <= self.max_angle <= 180 and 
                self.count > 0)


class DriveSwingOfBlock(BaseBlock):
    """先定位再摆动块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('drive_swing_of', fields, statements)
        self.channel = self.get_field('channel', 'left')
        self.target = self.get_field('target', 90)
        self.approach_speed = self.get_field('approach_speed', 40)
        self.amplitude = self.get_field('amplitude', 60)
        self.swing_speed = self.get_field('swing_speed', 30)
        self.count = self.get_field('count', 5)
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行先定位再摆动"""
        if not interfaces.drive:
            logger.warning("[DriveSwingOfBlock] Drive interface not available")
            return
        
        logger.info(f"[DriveSwingOfBlock] {self.channel} 摆动到 {self.target}° "
                   f"(±{self.amplitude}°), 次数: {self.count}")
        
        try:
            await interfaces.drive.servo_swing_of(
                self.channel, self.target, self.approach_speed,
                self.amplitude, self.swing_speed, self.count
            )
        except Exception as e:
            logger.error(f"[DriveSwingOfBlock] 失败: {e}")
            raise
    
    def validate(self) -> bool:
        """验证参数有效"""
        return (self.channel in ['left', 'right'] and 
                0 <= self.target <= 180 and 
                self.count > 0)


class DriveDistancePidBlock(BaseBlock):
    """PID 精确距离移动块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('drive_distance_pid', fields, statements)
        self.distance = self.get_field('distance', 0)
        self.speed = self.get_field('speed', 1)
        self.direction = self.get_field('direction', 0)
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行 PID 精确距离移动"""
        if not interfaces.drive:
            logger.warning("[DriveDistancePidBlock] Drive interface not available")
            return
        
        direction_str = "后退" if self.direction == 0 else "前进"
        logger.info(f"[DriveDistancePidBlock] PID 驱动 {direction_str}: {self.distance}cm, "
                   f"速度={self.speed}%, direction={self.direction}")
        
        try:
            await interfaces.drive.move_distance_pid(
                self.distance,
                self.speed,
                self.direction
            )
            logger.debug(f"[DriveDistancePidBlock] PID 移动完成（{direction_str}）")
        except Exception as e:
            logger.error(f"[DriveDistancePidBlock] PID 移动失败: {e}")
            raise
    
    def validate(self) -> bool:
        """验证参数有效"""
        return self.distance >= 0 and self.speed > 0 and self.direction in [0, 1]


class TurnDegPidBlock(BaseBlock):
    """PID 精确转向块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('turn_deg_pid', fields, statements)
        self.angle = self.get_field('driveRotate', 0)
        self.is_center = self.get_field('isCenter', True)
        self.speed = self.get_field('speed', 1)
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行 PID 精确转向"""
        if not interfaces.drive:
            logger.warning("[TurnDegPidBlock] Drive interface not available")
            return
        
        # 处理正负角度：正数右转，负数左转
        if self.angle < 0:
            angle_abs = abs(self.angle)
            rotate_type = "原地" if self.is_center else "移动"
            logger.info(f"[TurnDegPidBlock] PID 左转: {angle_abs}° ({rotate_type}旋转), 速度={self.speed}")
            try:
                await interfaces.drive.rotate_left_pid(angle_abs, self.is_center, self.speed)
            except Exception as e:
                logger.error(f"[TurnDegPidBlock] PID 左转失败: {e}")
                raise
        else:
            rotate_type = "原地" if self.is_center else "移动"
            logger.info(f"[TurnDegPidBlock] PID 右转: {self.angle}° ({rotate_type}旋转), 速度={self.speed}")
            try:
                await interfaces.drive.rotate_right_pid(self.angle, self.is_center, self.speed)
            except Exception as e:
                logger.error(f"[TurnDegPidBlock] PID 右转失败: {e}")
                raise
        
        logger.debug(f"[TurnDegPidBlock] PID 转向完成")
    
    def validate(self) -> bool:
        """验证参数有效"""
        return self.speed > 0


class DriveStopBlock(BaseBlock):
    """电机停止块"""
    
    def __init__(self, fields: Dict[str, Any], statements: Optional[Dict[str, List['AnimationBlock']]] = None):
        super().__init__('drive_stop', fields, statements)
        self.brake = self.get_field('brake', 0)  # 0=普通停止, 1=紧急刹车
    
    async def execute(self, interfaces: HardwareInterfaces) -> None:
        """执行停止"""
        if not interfaces.drive:
            logger.warning("[DriveStopBlock] Drive interface not available")
            return
        
        if self.brake:
            logger.info("[DriveStopBlock] 执行紧急刹车")
            await interfaces.drive.brake()
        else:
            logger.info("[DriveStopBlock] 执行普通停止")
            await interfaces.drive.stop()
    
    def validate(self) -> bool:
        """验证参数有效"""
        return self.brake in [0, 1]

