"""
配置和数据类定义

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from dataclasses import dataclass, field
from enum import Enum, auto
from typing import Optional, Tuple
from pathlib import Path


class LcdSide(Enum):
    """LCD 侧别"""
    LEFT = 0
    RIGHT = 1
    BOTH = 2


class LidType(Enum):
    """眼睑类型"""
    NONE = 0
    TOP = auto()      # 上眼睑 (T 后缀)
    BOTTOM = auto()   # 下眼睑 (B 后缀)
    SIDE = auto()     # 侧眼睑 (L/R 后缀)


@dataclass
class EyeState:
    """
    单眼状态
    
    Attributes:
        iris_theme: 虹膜主题 (CLASSIC, MODERN, SPACE, DIGI, ORBIT, GLOW, FOOD, MISC, SEASONAL, SYMBOL, CASINO, MATCH)
        iris_style: 虹膜样式/颜色 (COLOR_BLUE, APPLE, etc.)
        iris_x: 虹膜 X 位置 (像素坐标，默认 120)
        iris_y: 虹膜 Y 位置 (像素坐标，默认 120)
        scale_x: X 缩放 (1.0 = 原始大小)
        scale_y: Y 缩放 (1.0 = 原始大小)
        rotation: 旋转角度 (度)
        top_lid_id: 上眼睑 ID
        bottom_lid_id: 下眼睑 ID
        top_lid_y: 上眼睑 Y 位置
        bottom_lid_y: 下眼睑 Y 位置
        background_type: 背景类型 (COLOR, IMAGE)
        background_style: 背景样式
        brightness: 亮度 (0-10)
    """
    # 虹膜
    iris_theme: str = "CLASSIC"
    iris_style: str = "COLOR_BLUE"
    iris_x: int = 120         # 像素坐标
    iris_y: int = 120         # 像素坐标
    scale_x: float = 1.0
    scale_y: float = 1.0
    rotation: float = 0.0
    
    # 眼睑
    top_lid_id: int = 0       # 0 = 无眼睑
    bottom_lid_id: int = 0
    top_lid_y: int = 0        # Y 位置偏移
    bottom_lid_y: int = 240   # Y 位置偏移
    
    # 背景 COLOR COLOR_BLACK | IMAGE HEARTS winter
    background_type: str = "IMAGE"
    background_style: str = "WINTER"
    
    # 其他
    brightness: int = 10
    
    def copy(self) -> 'EyeState':
        """创建状态副本"""
        return EyeState(
            iris_theme=self.iris_theme,
            iris_style=self.iris_style,
            iris_x=self.iris_x,
            iris_y=self.iris_y,
            scale_x=self.scale_x,
            scale_y=self.scale_y,
            rotation=self.rotation,
            top_lid_id=self.top_lid_id,
            bottom_lid_id=self.bottom_lid_id,
            top_lid_y=self.top_lid_y,
            bottom_lid_y=self.bottom_lid_y,
            background_type=self.background_type,
            background_style=self.background_style,
            brightness=self.brightness,
        )


@dataclass
class EngineConfig:
    """
    引擎配置
    
    Attributes:
        base_path: Doly 基础路径
        lcd_lib_path: LCD 库路径
        default_fps: 默认帧率
        default_fps_seq: 序列文件默认帧率
        auto_blink: 是否启用自动眨眼
        blink_interval: 眨眼间隔范围 (秒)
        blink_animations: 眨眼动画列表 {名称: 权重}
        use_mock: 使用模拟驱动
    """
    project_root: str = "/home/pi/dolydev"
    config_path: str = "/home/pi/dolydev/assets"
    base_path: str = "/.doly"
    lcd_lib_path: str = "/home/pi/dolydev/libs/Doly/libs/libLcdControl.so"
    default_fps: int = 20
    default_fps_seq: int = 15
    auto_blink: bool = False
    blink_interval: Tuple[float, float] = (3.0, 8.0)
    blink_animations: dict = field(default_factory=dict)
    use_mock: bool = False
    passive_mode: bool = False

    # 视频流（FaceReco 推流）配置
    video_stream_enabled: bool = False
    video_stream_resource_id: str = "facereco_video"
    video_stream_instance_id: int = 0
    video_stream_target_lcd: str = "RIGHT"
    video_stream_fps: int = 15
    video_stream_timeout_ms: int = 100
    video_stream_display_mode: str = "overlay"  # overlay / exclusive
    video_stream_overlay_style: str = "full"    # full / pupil
    video_stream_pupil_radius_ratio: float = 0.35  # 瞳孔半径比例 (0.0-1.0)

    # 自动复位相关
    auto_reset_enabled: bool = True
    auto_reset_delay_ms: int = 300
    auto_reset_expression: str = ""
    auto_reset_restore_passive: bool = True
    auto_reset_background: bool = True
    
    # 任务优先级系统
    priority_enabled: bool = False
    
    # ZMQ 配置
    zmq_cmd_endpoint: str = "ipc:///tmp/doly_eye_cmd.sock"
    zmq_event_endpoint: str = "ipc:///tmp/doly_eye_event.sock"
    
    @property
    def images_path(self) -> Path:
        """图像资源路径"""
        return Path(self.base_path) / "images"
    
    @property
    def iris_path(self) -> Path:
        """虹膜资源路径"""
        return self.images_path / "iris"
    
    @property
    def lids_path(self) -> Path:
        """眼睑资源路径"""
        return self.images_path / "lids"
    
    @property
    def background_path(self) -> Path:
        """背景资源路径"""
        return self.images_path / "background"
    
    @property
    def animations_path(self) -> Path:
        """动画序列资源路径 (images/animations)"""
        return self.images_path / "animations"

    @property
    def config_dir(self) -> Path:
        """配置文件基础目录 (config)"""
        return Path(self.config_path) / "config"
    
    @property
    def eye_config_dir(self) -> Path:
        """眼睛配置文件目录 (config/eye)"""
        return self.config_dir / "eye"

    def resolve_asset_path(self, path_str: str) -> Path:
        """
        统一解析资源路径
        
        处理 /.doly/ 等旧路径前缀
        """
        if not path_str:
            return Path()
            
        # 处理 /.doly/ 前缀
        if path_str.startswith("/.doly/"):
            # 去掉 /.doly/，并与 base_path 拼接
            # 例如 /.doly/images/iris/... -> assets/images/iris/...
            relative_path = path_str[len("/.doly/"):]
            return Path(self.base_path) / relative_path
            
        p = Path(path_str)
        if p.is_absolute():
            return p
            
        # 默认相对于 images_path
        return self.images_path / path_str


# 表情预设（像素坐标，屏幕中心为 120,120）
# 注意: 眼睑 ID 参考 config/eye/eyelid.xml
EXPRESSIONS = {
    "normal": {
        "left": EyeState(),
        "right": EyeState(),
    },
    "happy": {
        # 开心: 眼睛微微向上，使用开心眼睑
        "left": EyeState(top_lid_id=4, iris_y=108),   # 向上 12 像素
        "right": EyeState(top_lid_id=4, iris_y=108),
    },
    "sad": {
        # 悲伤: 眼睛向下，使用悲伤眼睑
        "left": EyeState(top_lid_id=3, iris_y=144),   # 向下 24 像素
        "right": EyeState(top_lid_id=3, iris_y=144),
    },
    "angry": {
        # 生气: 眼睛微微向上，使用生气眼睑
        "left": EyeState(top_lid_id=2, iris_y=102),   # 向上 18 像素
        "right": EyeState(top_lid_id=2, iris_y=102),
    },
    "surprised": {
        # 惊讶: 无眼睑（眼睛睁大）
        "left": EyeState(top_lid_id=0, bottom_lid_id=0),
        "right": EyeState(top_lid_id=0, bottom_lid_id=0),
    },
    "sleepy": {
        # 困倦: 眼睛向下，使用半闭眼睑
        "left": EyeState(top_lid_id=6, iris_y=156),   # 向下 36 像素
        "right": EyeState(top_lid_id=6, iris_y=156),
    },
    "wink_left": {
        # 左眼眨眼
        "left": EyeState(top_lid_id=7),  # 闭眼眼睑
        "right": EyeState(),
    },
    "wink_right": {
        # 右眼眨眼
        "left": EyeState(),
        "right": EyeState(top_lid_id=7),  # 闭眼眼睑
    },
    "look_left": {
        # 看左边
        "left": EyeState(iris_x=24),    # 向左 96 像素
        "right": EyeState(iris_x=24),
    },
    "look_right": {
        # 看右边
        "left": EyeState(iris_x=216),   # 向右 96 像素
        "right": EyeState(iris_x=216),
    },
    "look_up": {
        # 看上面
        "left": EyeState(iris_y=24),    # 向上 96 像素
        "right": EyeState(iris_y=24),
    },
    "look_down": {
        # 看下面
        "left": EyeState(iris_y=216),   # 向下 96 像素
        "right": EyeState(iris_y=216),
    },
}
