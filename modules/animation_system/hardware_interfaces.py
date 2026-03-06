"""
hardware_interfaces.py

硬件接口抽象定义（在 animation_system 目录中的新文件）
这个文件定义了所有硬件接口的抽象基类，避免循环导入问题。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from abc import ABC, abstractmethod
from typing import Optional


class EyeInterface(ABC):
    """眼睛动画接口"""
    
    @abstractmethod
    async def play_animation(self, category: str, animation: str, priority: int = 5, hold_duration: float = 0.0) -> None:
        """
        播放眼睛动画
        
        Args:
            category: 动画分类（如 HAPPINESS, DEPRESSION）
            animation: 动画名称（如 EXCITED, BLINK）
            priority: 优先级 (1-10)
            hold_duration: 动画播放完后保持状态的时长（秒）
        """
        pass
    
    @abstractmethod
    async def play_behavior(self, behavior: str, level: int = 1, priority: int = 5, hold_duration: float = 0.0) -> None:
        """
        播放行为动画
        
        Args:
            behavior: 行为名称
            level: 等级
            priority: 优先级 (1-10)
            hold_duration: 动画播放完后保持状态的时长（秒）
        """
        pass
    
    @abstractmethod
    async def stop_animation(self) -> None:
        """停止当前眼睛动画"""
        pass
    
    @abstractmethod
    async def play_sequence_animations(self, sequence: str, side: str = 'BOTH', loop: bool = False, fps: Optional[int] = None, speed: float = 1.0) -> Optional[str]:
        """
        在眼睛上叠加播放一个 .seq 序列

        Args:
            sequence: 序列名称或文件名
            side: 目标侧别 ('LEFT','RIGHT','BOTH')
            loop: 是否循环
            fps: 帧率
            speed: 播放速度（保留参数）

        Returns:
            overlay_id 如果成功，否则 None
        """
        pass

    @abstractmethod
    async def stop_overlay_sequence(self, overlay_id: str) -> bool:
        """
        停止指定的 overlay

        Args:
            overlay_id: overlay 的 id

        Returns:
            True 如果成功停止
        """
        pass
    
    @abstractmethod
    async def play_overlay_image(self, image: str, side: str = 'BOTH', loop: bool = False, fps: Optional[int] = None, x: int = 0, y: int = 0, scale: float = 1.0, rotation: float = 0.0, duration_ms: Optional[int] = None, delay_ms: Optional[int] = None) -> Optional[str]:
        """
        在眼睛上叠加播放一张 PNG 图片（可选简单动画）

        Args:
            image: 图片路径或名称
            side: 目标侧别 ('LEFT','RIGHT','BOTH')
            loop: 是否循环
            fps: 帧率（用于动画）
            x, y: 相对于屏幕中心的偏移（像素）
            scale: 缩放系数
            rotation: 旋转角度（度）
            duration_ms: 持续时长（毫秒），None 表示直到手动停止或 loop=True
            delay_ms: 启动前延时（毫秒）

        Returns:
            overlay_id 如果成功，否则 None
        """
        pass

    @abstractmethod
    async def stop_overlay_image(self, overlay_id: str) -> bool:
        """
        停止指定的图片 overlay

        Args:
            overlay_id: overlay 的 id

        Returns:
            True 如果成功停止
        """
        pass

    @abstractmethod
    async def set_background(self, style: str, bg_type: str = "COLOR", side: str = 'BOTH', duration_ms: int = 0) -> None:
        """
        设置眼睛背景
        
        Args:
            style: 背景样式
            bg_type: 背景类型（'IMAGE' 或 'COLOR'）
            side: 目标侧别 ('LEFT','RIGHT','BOTH')
            duration_ms: 显示持续时间（毫秒），0 表示永久
        """
        pass


class LEDInterface(ABC):
    """LED 灯光接口"""
    
    @abstractmethod
    async def set_color(self, color: str, side: int = 0, duration_ms: int = 0,
                       hold_duration: int = 0, 
                       default_color: str = '#000000') -> None:
        """
        设置 LED 静态颜色
        
        Args:
            color: 颜色值（十六进制格式，如 '#ffffff'）
            side: 灯光位置（0=全部，1=左侧，2=右侧）
            duration_ms: 显示持续时间（毫秒，0表示不等待）
            hold_duration: 保持持续时长（毫秒，0表示不自动恢复）
            default_color: 超时后恢复的默认颜色（十六进制格式）
        """
        pass
    
    @abstractmethod
    async def set_color_with_fade(
        self, 
        color: str, 
        duration_ms: int,
        fade_color: Optional[str] = None,
        side: int = 0,
        hold_duration: int = 0,
        default_color: str = '#000000'
    ) -> None:
        """
        设置 LED 颜色并可选渐变
        
        Args:
            color: 起始颜色
            duration_ms: 持续时间（毫秒）
            fade_color: 渐变目标颜色（可选）
            side: 灯光位置
            hold_duration: 保持持续时长（毫秒，0表示不自动恢复）
            default_color: 超时后恢复的默认颜色（十六进制格式）
        """
        pass
    
    @abstractmethod
    async def turn_off(self, side: int = 0) -> None:
        """
        关闭 LED
        
        Args:
            side: 灯光位置
        """
        pass


class SoundInterface(ABC):
    """声音播放接口"""
    
    @abstractmethod
    async def play(self, type_id: str, name: str, wait: bool = True) -> None:
        """
        播放声音
        
        Args:
            type_id: 声音类型（如 DOLY, MUSIC, SFX）
            name: 声音名称
            wait: 是否等待播放完成
        """
        pass
    
    @abstractmethod
    async def stop(self) -> None:
        """停止当前声音"""
        pass
    
    @abstractmethod
    def is_playing(self) -> bool:
        """检查是否正在播放"""
        pass


class TTSInterface(ABC):
    """TTS 语音合成接口"""
    
    @abstractmethod
    async def synthesize(self, request: dict) -> dict:
        """
        合成语音
        
        Args:
            request: TTS 请求字典，包含以下字段：
                - action: "tts.synthesize"
                - text: 要合成的文本
                - voice: 发音人（如 "zh-CN-XiaoxiaoNeural"）
                - pitch: 音调（如 "+5Hz"）
                - rate: 语速（如 "+10%"）
                - volume: 音量（如 "+5%"）
                - play: 是否播放（True/False）
                - play_mode: 播放模式（"audio_player" 或 "local"）
                - format: 音频格式（"wav" 或 "mp3"）
                
        Returns:
            响应字典，包含：
                - ok: 是否成功
                - path: 生成的音频文件路径
                - format: 音频格式
                - sample_rate: 采样率
                - playback: 播放信息（如果 play=True）
        """
        pass


class ArmInterface(ABC):
    """手臂控制接口"""
    
    @abstractmethod
    async def set_angle(self, angle: float, side: int = 0, speed: float = 50, brake: int = 0,
                       en_servo_autohold: bool = False, servo_autohold_duration: int = 3000) -> None:
        """
        设置手臂角度
        
        Args:
            angle: 目标角度（0-180度）
            side: 手臂位置（0=全部，1=左侧，2=右侧）
            speed: 移动速度（1-100，默认50）
            brake: 制动模式（0=不制动，1=制动，默认0）
            en_servo_autohold: 是否启用舵机自动保持（默认False）
            servo_autohold_duration: 自动保持时长（毫秒，默认3000）
        """
        pass
    
    @abstractmethod
    async def get_angle(self, side: int = 1) -> float:
        """
        获取当前手臂角度
        
        Args:
            side: 手臂位置（1=左侧，2=右侧）
            
        Returns:
            当前角度
        """
        pass


class DriveInterface(ABC):
    """驱动控制接口"""
    
    @abstractmethod
    async def move_distance(
        self,
        distance: float,
        speed: float,
        direction: int = 0,
        accel: float = 20,
        brake: float = 0
    ) -> None:
        """
        直线移动指定距离
        
        Args:
            distance: 移动距离（厘米）
            speed: 速度
            direction: 方向（0=前进，1=后退）
            accel: 加速度
            brake: 制动值
        """
        pass
    
    @abstractmethod
    async def rotate_left(
        self,
        angle: float,
        is_center: bool = True,
        speed: float = 1,
        accel: float = 20,
        brake: float = 0
    ) -> None:
        """
        左转
        
        Args:
            angle: 旋转角度
            is_center: 是否原地旋转
            speed: 速度
            accel: 加速度
            brake: 制动值
        """
        pass
    
    @abstractmethod
    async def rotate_right(
        self,
        angle: float,
        is_center: bool = True,
        speed: float = 1,
        accel: float = 20,
        brake: float = 0
    ) -> None:
        """
        右转
        
        Args:
            angle: 旋转角度
            is_center: 是否原地旋转
            speed: 速度
            accel: 加速度
            brake: 制动值
        """
        pass
    
    @abstractmethod
    async def stop(self) -> None:
        """停止移动"""
        pass

    @abstractmethod
    async def brake(self) -> None:
        """紧急刹车：立即停止"""
        pass

    @abstractmethod
    async def move_pulses(self, pulses: int, throttle: float) -> None:
        """
        按编码器脉冲移动
        
        Args:
            pulses: 脉冲数（正数为前进，负数为后退）
            throttle: 油门大小（0.0-1.0）
        """
        pass

    @abstractmethod
    async def motor_control(self, left_throttle: float, right_throttle: float, duration: float = 0.0) -> None:
        """
        直接控制电机油门
        
        Args:
            left_throttle: 左电机油门（-1.0 到 1.0）
            right_throttle: 右电机油门（-1.0 到 1.0）
            duration: 持续时间（秒），0 表示持续运行
        """
        pass

    @abstractmethod
    async def motor_forward(self, speed: float, duration: float = 0.0) -> None:
        """电机前进"""
        pass

    @abstractmethod
    async def motor_backward(self, speed: float, duration: float = 0.0) -> None:
        """电机后退"""
        pass

    @abstractmethod
    async def motor_turn_left(self, speed: float, duration: float = 0.0) -> None:
        """左转"""
        pass

    @abstractmethod
    async def motor_turn_right(self, speed: float, duration: float = 0.0) -> None:
        """右转"""
        pass

    @abstractmethod
    async def move_distance_cm(self, distance_cm: float, speed: float, timeout_ms: int = 10000) -> None:
        """
        按真实距离移动电机
        
        Args:
            distance_cm: 移动距离（厘米），正数=前进，负数=后退
            speed: 速度百分比（0-100）
            timeout_ms: 超时时间（毫秒）
        """
        pass


class HardwareInterfaces:
    """硬件接口集合"""
    
    def __init__(
        self,
        eye: Optional[EyeInterface] = None,
        led: Optional[LEDInterface] = None,
        sound: Optional[SoundInterface] = None,
        arm: Optional[ArmInterface] = None,
        drive: Optional[DriveInterface] = None,
        tts: Optional[TTSInterface] = None
    ):
        """
        初始化硬件接口
        
        Args:
            eye: 眼睛动画接口
            led: LED 灯光接口
            sound: 声音播放接口
            arm: 手臂控制接口
            drive: 驱动控制接口
            tts: TTS 语音合成接口
        """
        self.eye = eye
        self.led = led
        self.sound = sound
        self.arm = arm
        self.drive = drive
        self.tts = tts
    
    def validate(self) -> None:
        """验证所有必需的接口是否已设置"""
        if self.eye is None:
            raise ValueError("Eye interface is required")
        if self.led is None:
            raise ValueError("LED interface is required")
        if self.sound is None:
            raise ValueError("Sound interface is required")
        if self.arm is None:
            raise ValueError("Arm interface is required")
        if self.drive is None:
            raise ValueError("Drive interface is required")
