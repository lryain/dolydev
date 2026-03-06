"""
配置文件加载器

解析 config/eye 目录下的 XML 配置文件

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from __future__ import annotations

import logging
import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Tuple

from .config import EngineConfig

logger = logging.getLogger(__name__)


# ============================================================================
# 数据类定义
# ============================================================================

@dataclass
class IrisConfig:
    """虹膜配置"""
    style: str              # 样式名 (COLOR_BLUE, APPLE, etc.)
    offset_x: int = 0       # X偏移
    offset_y: int = 0       # Y偏移
    left_path: str = ""     # 左眼图片路径
    right_path: str = ""    # 右眼图片路径 (可选，默认复制左眼)


@dataclass
class IrisTypeConfig:
    """虹膜类型配置"""
    name: str                           # 类型名 (CLASSIC, MODERN, etc.)
    irises: Dict[str, IrisConfig] = field(default_factory=dict)  # style -> config


@dataclass
class BackgroundConfig:
    """背景配置"""
    style: str              # 样式名
    left_path: str = ""     # 左眼背景路径 (IMAGE类型)
    right_path: str = ""    # 右眼背景路径


@dataclass
class BackgroundTypeConfig:
    """背景类型配置"""
    name: str               # 类型名 (COLOR, IMAGE)
    backgrounds: Dict[str, BackgroundConfig] = field(default_factory=dict)


@dataclass
class LidImageConfig:
    """眼睑图片配置"""
    lid_id: int             # 眼睑 ID
    name: str               # 名称
    path: str               # 图片路径
    fill_r: int = 0         # 填充颜色 R
    fill_g: int = 0         # 填充颜色 G
    fill_b: int = 0         # 填充颜色 B
    fill_a: int = 0         # 填充颜色 Alpha


@dataclass
class LidAnimationConfig:
    """眼睑动画配置"""
    lid_id: int             # 眼睑 ID
    name: str               # 名称
    path: str               # .seq 文件路径
    width: int = 240        # 宽度
    height: int = 240       # 高度
    frame_total: int = 0    # 总帧数
    ratio: int = 1          # 帧率分频器
    loop: int = 0           # 循环次数 (0=无限)


@dataclass
class EyelidConfig:
    """眼睑配置总览"""
    images: Dict[int, LidImageConfig] = field(default_factory=dict)      # id -> config
    animations: Dict[int, LidAnimationConfig] = field(default_factory=dict)


@dataclass
class AnimationSequence:
    """动画序列 (对应一个 blockly XML 文件中的 eye_animations 部分)"""
    category: str
    animation: str
    start_ms: int = 0


@dataclass
class AnimationBlock:
    """块配置 (对应 animationlist.xml 中的 AnimationBlock)"""
    file_name: str
    sequences: List[AnimationSequence] = field(default_factory=list)


@dataclass
class AnimationListCategory:
    """动画列表分类 (对应 animationlist.xml 中的分类标签)"""
    name: str
    blocks: List[AnimationBlock] = field(default_factory=list)


@dataclass
class EyeAnimationEvent:
    """眼睛动画关键帧"""
    x: int = 120            # 虹膜 X 位置
    y: int = 120            # 虹膜 Y 位置
    time_ms: int = 0        # 过渡时间 (ms)
    wait: int = 0           # 等待时间 (ms)
    scale_x: float = 1.0    # X 缩放
    scale_y: float = 1.0    # Y 缩放
    lid_top_y: int = 0      # 上眼睑 Y 位置
    lid_bot_y: int = 240    # 下眼睑 Y 位置
    top_lid_index: int = 0  # 上眼睑 ID
    bot_lid_index: int = 0  # 下眼睑 ID
    rotate_angle: float = 0 # 旋转角度
    ellipse_h: int = 0      # 椭圆高度


@dataclass
class EyeAnimationSide:
    """单边眼睛动画"""
    side: str               # "Left" 或 "Right"
    events: List[EyeAnimationEvent] = field(default_factory=list)


@dataclass
class EyeAnimation:
    """眼睛动画定义"""
    name: str               # 动画名称
    anim_id: int            # 动画 ID
    categories: List[str] = field(default_factory=list)  # 分类标签
    iris_id: int = 0        # 指定虹膜 ID (0=当前)
    background_id: int = 0  # 指定背景 ID (0=当前)
    left: Optional[EyeAnimationSide] = None
    right: Optional[EyeAnimationSide] = None


# ============================================================================
# 配置加载器
# ============================================================================

class EyeConfigLoader:
    """
    眼睛配置加载器
    
    加载 config/eye 目录下的所有配置文件
    """
    
    def __init__(self, config_dir: Optional[str | Path] = None):
        if config_dir:
            self._config_dir = Path(config_dir)
        else:
            # 统一使用 EngineConfig 中的路径
            self._config_dir = EngineConfig().eye_config_dir
            
        self._iris_types: Dict[str, IrisTypeConfig] = {}
        self._background_types: Dict[str, BackgroundTypeConfig] = {}
        self._eyelid_config: Optional[EyelidConfig] = None
        self._eye_animations: Dict[str, EyeAnimation] = {}
        self._eye_animations_by_id: Dict[int, EyeAnimation] = {}
        self._animation_list: Dict[str, AnimationListCategory] = {}  # category_name -> config
        self._loaded = False

    def load_all(self):
        """
        加载所有配置文件
        
        Returns:
            成功返回 True
        """
        if self._loaded:
            return True
            
        try:
            self._load_iris_types()
            self._load_background_types()
            self._load_eyelid_config()
            self._load_eye_animations()
            # behavior 由 BehaviorManager 独立管理
            self._loaded = True
            logger.info("所有眼睛配置加载完成")
            return True
        except Exception as e:
            logger.error(f"加载配置失败: {e}")
            return False
    
    # ------------------------------------------------------------------------
    # 虹膜配置
    # ------------------------------------------------------------------------
    
    def _load_iris_types(self):
        """加载虹膜类型配置"""
        path = self._config_dir / "iris_type.xml"
        if not path.exists():
            logger.warning(f"虹膜配置文件不存在: {path}")
            return
            
        tree = ET.parse(path)
        root = tree.getroot()
        
        for iris_type_elem in root.findall("Iris_Type"):
            type_name = iris_type_elem.get("name", "UNKNOWN")
            iris_type = IrisTypeConfig(name=type_name)
            
            for iris_elem in iris_type_elem.findall("Iris"):
                style = iris_elem.get("style", "")
                config = IrisConfig(
                    style=style,
                    offset_x=int(iris_elem.get("offsetX", "0")),
                    offset_y=int(iris_elem.get("offsetY", "0")),
                    left_path=iris_elem.get("left", ""),
                    right_path=iris_elem.get("right", "")
                )
                # 如果 right 未定义，使用 left
                if not config.right_path:
                    config.right_path = config.left_path
                    
                iris_type.irises[style] = config
            
            self._iris_types[type_name] = iris_type
        
        logger.info(f"加载了 {len(self._iris_types)} 种虹膜类型")
    
    def get_iris_types(self) -> List[str]:
        """获取所有虹膜类型名称"""
        return list(self._iris_types.keys())
    
    def get_iris_styles(self, type_name: str) -> List[str]:
        """获取指定类型的所有虹膜样式"""
        if type_name not in self._iris_types:
            return []
        return list(self._iris_types[type_name].irises.keys())
    
    def get_iris_config(self, type_name: str, style: str) -> Optional[IrisConfig]:
        """获取虹膜配置"""
        if type_name not in self._iris_types:
            return None
        return self._iris_types[type_name].irises.get(style)
    
    # ------------------------------------------------------------------------
    # 背景配置
    # ------------------------------------------------------------------------
    
    def _load_background_types(self):
        """加载背景类型配置"""
        path = self._config_dir / "background_type.xml"
        if not path.exists():
            logger.warning(f"背景配置文件不存在: {path}")
            return
            
        tree = ET.parse(path)
        root = tree.getroot()
        
        for bg_type_elem in root.findall("Background_Type"):
            type_name = bg_type_elem.get("name", "UNKNOWN")
            bg_type = BackgroundTypeConfig(name=type_name)
            
            for bg_elem in bg_type_elem.findall("Background"):
                style = bg_elem.get("style", "")
                config = BackgroundConfig(
                    style=style,
                    left_path=bg_elem.get("left", ""),
                    right_path=bg_elem.get("right", "")
                )
                if not config.right_path:
                    config.right_path = config.left_path
                    
                bg_type.backgrounds[style] = config
            
            self._background_types[type_name] = bg_type
        
        logger.info(f"加载了 {len(self._background_types)} 种背景类型")
    
    def get_background_types(self) -> List[str]:
        """获取所有背景类型名称"""
        return list(self._background_types.keys())
    
    def get_background_styles(self, type_name: str) -> List[str]:
        """获取指定类型的所有背景样式"""
        if type_name not in self._background_types:
            return []
        return list(self._background_types[type_name].backgrounds.keys())
    
    def get_background_config(self, type_name: str, style: str) -> Optional[BackgroundConfig]:
        """获取背景配置"""
        if type_name not in self._background_types:
            return None
        return self._background_types[type_name].backgrounds.get(style)
    
    # ------------------------------------------------------------------------
    # 眼睑配置
    # ------------------------------------------------------------------------
    
    def _load_eyelid_config(self):
        """加载眼睑配置"""
        path = self._config_dir / "eyelid.xml"
        if not path.exists():
            logger.warning(f"眼睑配置文件不存在: {path}")
            return
            
        tree = ET.parse(path)
        root = tree.getroot()
        
        self._eyelid_config = EyelidConfig()
        
        # 加载静态眼睑图片
        images_elem = root.find("Images")
        if images_elem is not None:
            for data_elem in images_elem.findall("Data"):
                lid_id = int(data_elem.get("id", "0"))
                config = LidImageConfig(
                    lid_id=lid_id,
                    name=data_elem.get("name", ""),
                    path=data_elem.get("path", ""),
                    fill_r=int(data_elem.get("r", "0")),
                    fill_g=int(data_elem.get("g", "0")),
                    fill_b=int(data_elem.get("b", "0")),
                    fill_a=int(data_elem.get("a", "0"))
                )
                self._eyelid_config.images[lid_id] = config
        
        # 加载眼睑动画
        animations_elem = root.find("Animations")
        if animations_elem is not None:
            for data_elem in animations_elem.findall("Data"):
                lid_id = int(data_elem.get("id", "0"))
                config = LidAnimationConfig(
                    lid_id=lid_id,
                    name=data_elem.get("name", ""),
                    path=data_elem.get("path", ""),
                    width=int(data_elem.get("w", "240")),
                    height=int(data_elem.get("h", "240")),
                    frame_total=int(data_elem.get("ft", "0")),
                    ratio=int(data_elem.get("ratio", "1")),
                    loop=int(data_elem.get("loop", "0"))
                )
                self._eyelid_config.animations[lid_id] = config
        
        logger.info(f"加载了 {len(self._eyelid_config.images)} 个眼睑图片, "
                   f"{len(self._eyelid_config.animations)} 个眼睑动画")
    
    def get_lid_image_config(self, lid_id: int) -> Optional[LidImageConfig]:
        """获取眼睑图片配置"""
        if self._eyelid_config is None:
            return None
        return self._eyelid_config.images.get(lid_id)
    
    def get_lid_animation_config(self, lid_id: int) -> Optional[LidAnimationConfig]:
        """获取眼睑动画配置"""
        if self._eyelid_config is None:
            return None
        return self._eyelid_config.animations.get(lid_id)
    
    def is_lid_animation(self, lid_id: int) -> bool:
        """检查眼睑 ID 是否为动画"""
        if self._eyelid_config is None:
            return False
        return lid_id in self._eyelid_config.animations
    
    def get_all_lid_ids(self) -> List[int]:
        """获取所有眼睑 ID"""
        if self._eyelid_config is None:
            return []
        ids = list(self._eyelid_config.images.keys())
        ids.extend(self._eyelid_config.animations.keys())
        return sorted(set(ids))
    
    # ------------------------------------------------------------------------
    # 眼睛动画配置
    # ------------------------------------------------------------------------
    
    def _load_eye_animations(self):
        """加载眼睛动画配置"""
        path = self._config_dir / "eyeanimations.xml"
        if not path.exists():
            logger.warning(f"眼睛动画配置文件不存在: {path}")
            return
            
        tree = ET.parse(path)
        root = tree.getroot()
        
        for anim_elem in root.findall("Animation"):
            anim = self._parse_animation(anim_elem)
            if anim:
                self._eye_animations[anim.name] = anim
                self._eye_animations_by_id[anim.anim_id] = anim
        
        logger.info(f"加载了 {len(self._eye_animations)} 个眼睛动画")
    
    def _parse_animation(self, elem: ET.Element) -> Optional[EyeAnimation]:
        """解析动画元素"""
        name = elem.get("Name", "")
        anim_id = int(elem.get("Id", "0"))
        categories_str = elem.get("Categories", "")
        categories = [c.strip() for c in categories_str.split(",") if c.strip()]
        
        anim = EyeAnimation(
            name=name,
            anim_id=anim_id,
            categories=categories,
            iris_id=int(elem.get("IrisId", "0")),
            background_id=int(elem.get("BackgroundId", "0"))
        )
        
        for side_elem in elem.findall("AnimationSide"):
            side_name = side_elem.get("Side", "")
            side = EyeAnimationSide(side=side_name)
            
            for event_elem in side_elem.findall("Event"):
                event = EyeAnimationEvent(
                    x=int(event_elem.get("X", "120")),
                    y=int(event_elem.get("Y", "120")),
                    time_ms=int(event_elem.get("TimeMs", "0")),
                    wait=int(event_elem.get("Wait", "0")),
                    scale_x=float(event_elem.get("ScaleX", "1")),
                    scale_y=float(event_elem.get("ScaleY", "1")),
                    lid_top_y=int(event_elem.get("LidTopY", "0")),
                    lid_bot_y=int(event_elem.get("LidBotY", "240")),
                    top_lid_index=int(event_elem.get("TopLidIndex", "0")),
                    bot_lid_index=int(event_elem.get("BotLidIndex", "0")),
                    rotate_angle=float(event_elem.get("RotateAngle", "0")),
                    ellipse_h=int(event_elem.get("Ellipse_H", "0"))
                )
                side.events.append(event)
            
            if side_name == "Left":
                anim.left = side
            elif side_name == "Right":
                anim.right = side
        
        return anim
    
    def get_animation_names(self) -> List[str]:
        """获取所有动画名称"""
        return list(self._eye_animations.keys())
    
    def get_animation_by_name(self, name: str) -> Optional[EyeAnimation]:
        """按名称获取动画"""
        return self._eye_animations.get(name)
    
    def get_animation_by_id(self, anim_id: int) -> Optional[EyeAnimation]:
        """按 ID 获取动画"""
        return self._eye_animations_by_id.get(anim_id)
    
    def get_animations_by_category(self, category: str) -> List[EyeAnimation]:
        """按分类获取动画列表"""
        result = []
        for anim in self._eye_animations.values():
            if category.upper() in [c.upper() for c in anim.categories]:
                result.append(anim)
        return result
    
    def get_animation(self, category_name: str, animation_name: str) -> Optional[EyeAnimation]:
        """获取指定分类和名称的动画"""
        animations = self.get_animations_by_category(category_name)
        for anim in animations:
            if anim.name == animation_name:
                return anim
        return None
    
    def get_all_categories(self) -> List[str]:
        """获取所有动画分类"""
        categories = set()
        for anim in self._eye_animations.values():
            categories.update(anim.categories)
        return sorted(categories)

    # ------------------------------------------------------------------------
    # 动画列表 (AnimationList) 加载
    # ------------------------------------------------------------------------
    
    def _load_animation_list(self):
        """加载 animationlist.xml 及相关的 blockly XML"""
        path = self._config_dir / "animations" / "animationlist.xml"
        if not path.exists():
            logger.warning(f"动画列表文件不存在: {path}")
            return
            
        try:
            tree = ET.parse(path)
            root = tree.getroot()
            
            for cat_elem in root:
                cat_name = cat_elem.tag
                category = AnimationListCategory(name=cat_name)
                
                for block_elem in cat_elem.findall("AnimationBlock"):
                    file_name = block_elem.text
                    if not file_name:
                        continue
                        
                    # 创建块配置
                    block = AnimationBlock(file_name=file_name)
                    
                    # 尝试解析该文件中的眼睛动画序列
                    seq_path = self._config_dir / "animations" / file_name
                    if seq_path.exists():
                        block.sequences = self._parse_blockly_eye_animations(seq_path)
                    
                    category.blocks.append(block)
                
                self._animation_list[cat_name] = category
                
            logger.info(f"加载了 {len(self._animation_list)} 个动画列表分类")
            
        except Exception as e:
            logger.error(f"加载 animationlist.xml 失败: {e}")

    def _parse_blockly_eye_animations(self, path: Path) -> List[AnimationSequence]:
        """解析 blockly XML 文件中的 eye_animations 节点"""
        sequences = []
        try:
            tree = ET.parse(path)
            root = tree.getroot()
            
            # blockly XML 通常有很多 block 节点
            # 我们寻找 type="eye_animations" 的 block
            for block in root.findall(".//block[@type='eye_animations']"):
                seq = AnimationSequence(category="", animation="")
                
                for field in block.findall("field"):
                    name = field.get("name")
                    value = field.text
                    if name == "category":
                        seq.category = value
                    elif name == "animation":
                        seq.animation = value
                    elif name == "start":
                        seq.start_ms = int(value) * 1000 # 假设是秒
                        
                if seq.category and seq.animation:
                    sequences.append(seq)
        except Exception as e:
            logger.error(f"解析 blockly 文件 {path} 失败: {e}")
            
        return sequences

    def get_animation_list_categories(self) -> List[str]:
        """获取所有动画列表分类名称"""
        return list(self._animation_list.keys())

    def get_blocks_by_list_category(self, category_name: str) -> List[AnimationBlock]:
        """获取指定分类下的所有块配置"""
        cat = self._animation_list.get(category_name)
        return cat.blocks if cat else []


# ============================================================================
# 全局配置实例
# ============================================================================

_global_config_loader: Optional[EyeConfigLoader] = None


def get_config_loader() -> EyeConfigLoader:
    """获取全局配置加载器实例"""
    global _global_config_loader
    if _global_config_loader is None:
        _global_config_loader = EyeConfigLoader()
        _global_config_loader.load_all()
    return _global_config_loader


def reload_config():
    """重新加载配置"""
    global _global_config_loader
    _global_config_loader = None
    return get_config_loader()
