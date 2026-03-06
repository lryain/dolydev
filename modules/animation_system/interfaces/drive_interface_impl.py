"""
Drive 系统接口实现

提供舵机和电机控制功能，通过 ZMQ IPC Socket 与 Drive 服务通信

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import json
import asyncio
import logging
import time
import zmq
from typing import Optional, Dict, Any
import sys
import os
from pathlib import Path

# 添加项目根目录到路径
project_root = Path(__file__).parent.parent.parent
sys.path.insert(0, str(project_root))

from modules.animation_system.hardware_interfaces import DriveInterface, ArmInterface

logger = logging.getLogger(__name__)


class DriveSystemInterface(DriveInterface, ArmInterface):
    """Drive 系统接口实现 - 舵机和电机控制"""
    
    def __init__(self, socket_path: str = 'ipc:///tmp/doly_control.sock'):
        """
        初始化 Drive 系统接口
        
        Args:
            socket_path: IPC Socket 路径
        """
        self.socket_path = socket_path
        self.ctx = zmq.Context()
        self.socket = self.ctx.socket(zmq.PUSH)
        try:
            self.socket.connect(self.socket_path)
            logger.info(f"[DriveSystemInterface] 连接到 {socket_path}")
        except Exception as e:
            logger.error(f"[DriveSystemInterface] 连接失败: {e}")
            self.socket = None
        
        # 初始化手臂角度缓存
        self._arm_angles = {"left": 90.0, "right": 90.0}
    
    async def _send_command(self, command: Dict[str, Any]) -> bool:
        """
        发送命令到 Drive 系统
        
        Args:
            command: 命令字典
            
        Returns:
            是否发送成功
        """
        if self.socket is None:
            logger.warning("[DriveSystemInterface] Socket 未连接")
            return False
        
        try:
            topic = "io.pca9535.control"
            # 记录发送时间戳，方便与 drive 服务端日志对齐
            ts = int(time.time() * 1000)
            logger.info(f"[DriveSystemInterface] {ts}ms -> SEND {topic}: {json.dumps(command)}")
            self.socket.send_string(topic, zmq.SNDMORE)
            self.socket.send_string(json.dumps(command))
            await asyncio.sleep(0.01)  # 让命令传递
            return True
        except Exception as e:
            logger.error(f"[DriveSystemInterface] 发送命令失败: {e}")
            return False
    
    async def enable_servo(self, channel: str = 'both', value: bool = True) -> None:
        """
        启用/禁用舵机
        
        Args:
            channel: 'left', 'right', 'both'
            value: True 启用, False 禁用
        """
        command = {
            "action": "enable_servo",
            "channel": channel,
            "value": value
        }
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 舵机 {channel}: {'启用' if value else '禁用'}")
    
    async def set_servo_angle(self, channel: str, angle: float, speed: float = 40) -> None:
        """
        设置单个舵机角度
        
        Args:
            channel: 'left' 或 'right'
            angle: 目标角度 (0-180)
            speed: 移动速度 (1-100)
        """
        command = {
            "action": "set_servo_multi",
            "targets": {channel: angle},
            "speed": speed
        }
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 设置 {channel} 舵机角度: {angle}°, 速度: {speed}")
    
    async def set_servo_both(self, left_angle: float, right_angle: float, speed: float = 40) -> None:
        """
        同时设置左右舵机角度
        
        Args:
            left_angle: 左舵机角度
            right_angle: 右舵机角度
            speed: 移动速度
        """
        command = {
            "action": "set_servo_multi",
            "targets": {"left": left_angle, "right": right_angle},
            "speed": speed
        }
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 设置舵机: left={left_angle}°, right={right_angle}°, 速度={speed}")
    
    async def servo_swing(self, channel: str, min_angle: float, max_angle: float, 
                         duration_ms: int = 400, count: int = 5) -> None:
        """
        舵机摆动
        
        Args:
            channel: 'left', 'right', 'all'
            min_angle: 最小角度
            max_angle: 最大角度
            duration_ms: 单次摆动持续时间 (ms)
            count: 摆动次数 (-1 为无限)
        """
        command = {
            "action": "servo_swing",
            "channel": channel,
            "min": min_angle,
            "max": max_angle,
            "duration": duration_ms,
            "count": count
        }
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 舵机摆动: {channel}, 范围: {min_angle}°-{max_angle}°, 次数: {count}")
    
    async def servo_swing_of(self, channel: str, target: float, approach_speed: float = 40,
                            amplitude: float = 60, swing_speed: float = 30, count: int = 5) -> None:
        """
        先定位再摆动 (以目标点为中心)
        
        Args:
            channel: 'left' 或 'right'
            target: 目标角度
            approach_speed: 靠近速度
            amplitude: 摆动幅度
            swing_speed: 摆动速度
            count: 摆动次数
        """
        command = {
            "action": "servo_swing_of",
            "channel": channel,
            "target": target,
            "approach_speed": approach_speed,
            "amplitude": amplitude,
            "swing_speed": swing_speed,
            "count": count
        }
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 摆动到 {channel}: {target}° (±{amplitude}°), 次数: {count}")
    
    async def servo_stop(self, channel: str = 'all') -> None:
        """
        停止舵机运动
        
        Args:
            channel: 'left', 'right', 'all'
        """
        command = {
            "action": "servo_stop",
            "channel": channel
        }
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 停止舵机: {channel}")
    
    async def set_servo_duration(self, channel: str, angle: float, duration_ms: int = 500) -> None:
        """
        在指定时间内移动舵机到目标角度
        
        Args:
            channel: 'left', 'right'
            angle: 目标角度
            duration_ms: 移动时间 (ms)
        """
        command = {
            "action": "set_servo_multi",
            "targets": {channel: angle},
            "duration": duration_ms
        }
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] {channel} 舵机在 {duration_ms}ms 内移至 {angle}°")
    
    # 驱动校准常量 (用于在不支持 move_distance 的后端上模拟)
    LINEAR_SPEED_CONVERSION = 5.0  # 速度 1.0 时约为 10cm/s
    ANGULAR_SPEED_CONVERSION = 90.0  # 速度 1.0 时约为 180度/s

    # 实现基类的抽象方法
    async def move_distance(self, distance: float, speed: float, direction: int = 0,
                           accel: float = 20, brake: float = 0) -> None:
        """
        直线移动指定距离
        
        注意：当前驱动后端不支持 native move_distance，使用 motor_forward/backward + duration 模拟
        """
        # 计算预计耗时
        duration = 0
        if speed > 0:
            duration = distance / (speed * self.LINEAR_SPEED_CONVERSION)
        
        if direction == 0:
            await self.motor_forward(speed, duration)
        else:
            await self.motor_backward(speed, duration)

        # 等待预计的物理执行时间再返回，确保上层在硬件完成动作后继续（便于同步）
        if duration and duration > 0:
            logger.debug(f"[DriveSystemInterface] 等待物理移动完成: {duration:.3f}s")
            try:
                await asyncio.sleep(duration)
            except asyncio.CancelledError:
                pass

        logger.info(f"[DriveSystemInterface] 模拟移动: {distance}cm, 速度: {speed}, 预计时间: {duration:.2f}s")
    
    async def rotate_left(self, angle: float, is_center: bool = True, speed: float = 1,
                         accel: float = 20, brake: float = 0) -> None:
        """
        左转
        """
        duration = 0
        if speed > 0:
            duration = angle / (speed * self.ANGULAR_SPEED_CONVERSION)
            
        await self.motor_turn_left(speed, duration)
        logger.info(f"[DriveSystemInterface] 模拟左转: {angle}°, 速度: {speed}, 预计时间: {duration:.2f}s")
    
    async def rotate_right(self, angle: float, is_center: bool = True, speed: float = 1,
                          accel: float = 20, brake: float = 0) -> None:
        """
        右转
        """
        duration = 0
        if speed > 0:
            duration = angle / (speed * self.ANGULAR_SPEED_CONVERSION)
            
        await self.motor_turn_right(speed, duration)
        logger.info(f"[DriveSystemInterface] 模拟右转: {angle}°, 速度: {speed}, 预计时间: {duration:.2f}s")
    
    async def stop(self) -> None:
        """
        停止驱动
        """
        command = {"action": "motor_stop"} # 修改为 motor_stop 确保电机停止
        await self._send_command(command)
        command = {"action": "servo_stop", "channel": "all"}
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 停止所有驱动")
    
    async def brake(self) -> None:
        """
        紧急刹车：立即停止
        """
        command = {"action": "motor_brake"}  # 使用紧急刹车命令
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 紧急刹车")
    
    async def move_pulses(self, pulses: int, throttle: float) -> None:
        """
        按编码器脉冲移动
        
        Args:
            pulses: 脉冲数
            throttle: 油门
        """
        command = {
            "action": "motor_move_pulses",
            "pulses": pulses,
            "throttle": throttle
        }
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 脉冲移动: {pulses}, 油门: {throttle}")
    
    async def motor_control(self, left_throttle: float, right_throttle: float, duration: float = 0.0) -> None:
        """
        控制电机油门
        
        基于油门符号选择合适的动作 (forward/backward/turn_left/turn_right)
        """
        if left_throttle > 0 and right_throttle > 0:
            await self.motor_forward((left_throttle + right_throttle) / 2, duration)
        elif left_throttle < 0 and right_throttle < 0:
            await self.motor_backward((abs(left_throttle) + abs(right_throttle)) / 2, duration)
        elif left_throttle < 0 and right_throttle > 0:
            await self.motor_turn_left((abs(left_throttle) + right_throttle) / 2, duration)
        elif left_throttle > 0 and right_throttle < 0:
            await self.motor_turn_right((left_throttle + abs(right_throttle)) / 2, duration)
        else:
            await self.stop()

    async def motor_forward(self, speed: float, duration: float = 0.0) -> None:
        """实现 motor_forward 动作"""
        command = {"action": "motor_forward", "speed": speed}
        if duration > 0: command["duration"] = duration
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 电机前进: speed={speed}, duration={duration}s")
        
        # 等待执行完成
        if duration > 0:
            try:
                await asyncio.sleep(duration)
            except asyncio.CancelledError:
                pass

    async def motor_backward(self, speed: float, duration: float = 0.0) -> None:
        """实现 motor_backward 动作"""
        command = {"action": "motor_backward", "speed": speed}
        if duration > 0: command["duration"] = duration
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 电机后退: speed={speed}, duration={duration}s")
        
        # 等待执行完成
        if duration > 0:
            try:
                await asyncio.sleep(duration)
            except asyncio.CancelledError:
                pass

    async def motor_turn_left(self, speed: float, duration: float = 0.0) -> None:
        """实现 motor_turn_left 动作"""
        command = {"action": "motor_turn_left", "speed": speed}
        if duration > 0: command["duration"] = duration
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 电机左转: speed={speed}, duration={duration}s")
        
        # 等待执行完成
        if duration > 0:
            try:
                await asyncio.sleep(duration)
            except asyncio.CancelledError:
                pass
    
    # ==================== PID 精确控制方法 ====================
    
    async def move_distance_pid(self, distance: float, speed: float, direction: int = 0, timeout_ms: int = 10000) -> None:
        """
        PID 精确直线移动指定距离
        
        Args:
            distance: 移动距离 (cm)，始终为正数
            speed: 速度百分比 (1-100)
            direction: 方向 (0=后退, 1=前进)
            timeout_ms: 超时时间 (毫秒)
        """
        direction_str = "后退" if direction == 0 else "前进"
        
        logger.info(f"[DriveSystemInterface] 发送 PID 移动命令: {direction_str} {distance}cm @ {speed}% (timeout={timeout_ms}ms)")
        
        command = {
            "action": "move_distance_cm_pid",
            "distance": distance,
            "speed": speed,
            "direction": direction,
            "timeout_ms": timeout_ms
        }
        logger.debug(f"[DriveSystemInterface] ZMQ 命令内容: {command}")
        await self._send_command(command)
        
        # 等待预计的物理执行时间
        duration = 0
        if speed > 0:
            duration = distance / (speed * self.LINEAR_SPEED_CONVERSION)
        
        if duration > 0:
            logger.debug(f"[DriveSystemInterface] 等待 PID 移动完成: {duration:.3f}s (超时设置={timeout_ms}ms)")
            try:
                await asyncio.sleep(duration)
            except asyncio.CancelledError:
                pass
        
        logger.info(f"[DriveSystemInterface] PID 移动完成（{direction_str}）: {distance}cm, 速度: {speed}%, 预计时间: {duration:.2f}s")
    
    async def rotate_left_pid(self, angle: float, is_center: bool = True, speed: float = 1) -> None:
        """
        PID 精确左转
        
        Args:
            angle: 转向角度 (度)
            is_center: 是否原地转向 (True=原地, False=移动时转)
            speed: 速度百分比 (1-100)
        """
        command = {
            "action": "turn_deg_pid",
            "angle": angle,
            "speed": speed,
            "is_center": is_center
        }
        await self._send_command(command)
        
        # 等待预计的物理执行时间
        duration = 0
        if speed > 0:
            duration = angle / (speed * self.ANGULAR_SPEED_CONVERSION)
        
        if duration > 0:
            logger.debug(f"[DriveSystemInterface] 等待 PID 左转完成: {duration:.3f}s")
            try:
                await asyncio.sleep(duration)
            except asyncio.CancelledError:
                pass
        
        mode_str = "原地" if is_center else "移动"
        logger.info(f"[DriveSystemInterface] PID 左转: {angle}°, 模式: {mode_str}, 速度: {speed}%, 预计时间: {duration:.2f}s")
    
    async def rotate_right_pid(self, angle: float, is_center: bool = True, speed: float = 1) -> None:
        """
        PID 精确右转
        
        Args:
            angle: 转向角度 (度)
            is_center: 是否原地转向 (True=原地, False=移动时转)
            speed: 速度百分比 (1-100)
        """
        command = {
            "action": "turn_deg_pid",
            "angle": -angle,  # 右转使用负角度
            "speed": speed,
            "is_center": is_center
        }
        await self._send_command(command)
        
        # 等待预计的物理执行时间
        duration = 0
        if speed > 0:
            duration = angle / (speed * self.ANGULAR_SPEED_CONVERSION)
        
        if duration > 0:
            logger.debug(f"[DriveSystemInterface] 等待 PID 右转完成: {duration:.3f}s")
            try:
                await asyncio.sleep(duration)
            except asyncio.CancelledError:
                pass
        
        mode_str = "原地" if is_center else "移动"
        logger.info(f"[DriveSystemInterface] PID 右转: {angle}°, 模式: {mode_str}, 速度: {speed}%, 预计时间: {duration:.2f}s")

    async def motor_turn_right(self, speed: float, duration: float = 0.0) -> None:
        """实现 motor_turn_right 动作"""
        command = {"action": "motor_turn_right", "speed": speed}
        if duration > 0: command["duration"] = duration
        await self._send_command(command)
        logger.info(f"[DriveSystemInterface] 电机右转: speed={speed}, duration={duration}s")
        
        # 等待执行完成
        if duration > 0:
            try:
                await asyncio.sleep(duration)
            except asyncio.CancelledError:
                pass
    
    async def move_distance_cm(self, distance_cm: float, speed: float, timeout_ms: int = 10000) -> None:
        """
        按真实距离移动电机
        
        Args:
            distance_cm: 移动距离（厘米），正数=前进，负数=后退
            speed: 速度百分比（0-100）
            timeout_ms: 超时时间（毫秒）
        """
        # 将百分比速度转换为油门（0-1）
        throttle = speed / 100.0
        
        logger.info(f"[DriveSystemInterface] 按距离移动: distance={distance_cm}cm, speed={speed}% (throttle={throttle:.2f}), timeout={timeout_ms}ms")
        
        command = {
            "action": "move_distance_cm",
            "distance_cm": distance_cm,
            "throttle": throttle,
            "timeout_ms": timeout_ms
        }
        await self._send_command(command)
        
        # 预估执行时间（基于距离和速度）
        # 假设全速时约为 10cm/s
        estimated_time = abs(distance_cm) / (10.0 * throttle) if throttle > 0 else 5.0
        estimated_time = min(estimated_time, timeout_ms / 1000.0)  # 不超过超时时间
        
        if estimated_time > 0:
            logger.debug(f"[DriveSystemInterface] 等待移动完成: {estimated_time:.2f}s")
            try:
                await asyncio.sleep(estimated_time)
            except asyncio.CancelledError:
                pass
        
        logger.info(f"[DriveSystemInterface] 距离移动完成: {distance_cm}cm")
    
    # 实现 ArmInterface
    async def set_angle(self, angle: float, side: int = 0, speed: float = 50, brake: int = 0,
                       en_servo_autohold: bool = False, servo_autohold_duration: int = 3000) -> None:
        """实现 ArmInterface.set_angle"""
        logger.info(f"[DriveSystemInterface.set_angle] 调用, angle={angle}, side={side}, speed={speed}, "
                   f"brake={brake}, autohold={en_servo_autohold}, hold_duration={servo_autohold_duration}ms")
        side_map = {0: "both", 1: "left", 2: "right"}
        channel = side_map.get(side, "both")
        
        logger.info(f"[DriveSystemInterface.set_angle] side={side} -> channel={channel}")
        
        # 计算舵机运动时间（关键！确保 repeat 块中命令不会堆积）
        # 舵机规格：标准舵机约为 60°/0.1s (速度=最大时)
        # 我们用速度 (1-100) 来缩放：速度越低，运动越慢
        # 角度变化 × (100/speed) / 600 = 时间(秒)
        current_angle_left = self._arm_angles.get("left", 90.0)
        current_angle_right = self._arm_angles.get("right", 90.0)
        
        if channel == "both":
            angle_delta_left = abs(angle - current_angle_left)
            angle_delta_right = abs(angle - current_angle_right)
            angle_delta = max(angle_delta_left, angle_delta_right)
        elif channel == "left":
            angle_delta = abs(angle - current_angle_left)
        else:
            angle_delta = abs(angle - current_angle_right)
        
        # 计算运动时间：标准舵机 60°/0.1s (speed=100)
        # servo_time = (angle_delta / 60) * (0.1 * (100 / speed))
        servo_time_sec = 0
        if angle_delta > 0.1:  # 只有角度变化足够大时才计算等待
            servo_time_sec = (angle_delta / 60.0) * (0.1 * (100.0 / max(speed, 1)))
            servo_time_ms = servo_time_sec * 1000
            logger.info(f"[DriveSystemInterface.set_angle] 角度变化={angle_delta:.1f}°, 速度={speed}, 预计运动时间={servo_time_ms:.0f}ms")
        
        # 构建包含自动保持参数的命令
        command = {
            "action": "set_servo_multi",
            "targets": {channel: angle} if channel != "both" else {"left": angle, "right": angle},
            "speed": speed,
            "brake": brake,
            "en_servo_autohold": en_servo_autohold,
            "servo_autohold_duration": servo_autohold_duration
        }
        
        autohold_str = f", autohold={servo_autohold_duration}ms" if en_servo_autohold else ""
        logger.info(f"[DriveSystemInterface.set_angle] 发送命令: {channel}={angle}°, speed={speed}, "
                   f"brake={brake}{autohold_str}")
        
        await self._send_command(command)
        
        # 等待舵机完成运动（重要！这样 repeat 块中的命令才不会堆积）
        if servo_time_sec > 0.001:
            logger.info(f"[DriveSystemInterface.set_angle] 等待舵机完成运动: {servo_time_sec*1000:.0f}ms")
            try:
                await asyncio.sleep(servo_time_sec)
            except asyncio.CancelledError:
                pass
        
        if channel == "both":
            self._arm_angles["left"] = angle
            self._arm_angles["right"] = angle
        else:
            self._arm_angles[channel] = angle
        
        logger.info(f"[DriveSystemInterface.set_angle] 完成")

    async def get_angle(self, side: int = 1) -> float:
        """实现 ArmInterface.get_angle (返回缓存值)"""
        channel = "left" if side == 1 else "right"
        return self._arm_angles.get(channel, 90.0)

    def __del__(self):
        """清理资源"""
        try:
            if self.socket:
                self.socket.close()
            self.ctx.term()
        except:
            pass
