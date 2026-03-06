"""
资源管理器 (基于 XML 配置)

负责根据 config/eye/*.xml 配置加载和缓存图像资源

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from __future__ import annotations

import logging
from pathlib import Path
from typing import Dict, List, Optional, Tuple, TYPE_CHECKING

if TYPE_CHECKING:
    pass

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False
    Image = None

from ..config import EngineConfig, LcdSide
from ..config_loader import (
    EyeConfigLoader, get_config_loader,
    IrisConfig, BackgroundConfig, LidImageConfig, LidAnimationConfig
)
from ..constants import LCD_WIDTH, LCD_HEIGHT
from ..exceptions import AssetNotFoundError

logger = logging.getLogger(__name__)


# 预定义颜色映射 (用于纯色背景)
COLOR_MAP = {
    "COLOR_BLACK": (0, 0, 0),
    "COLOR_WHITE": (255, 255, 255),
    "COLOR_GRAY": (128, 128, 128),
    "COLOR_SALMON": (250, 128, 114),
    "COLOR_RED": (255, 0, 0),
    "COLOR_DARK_RED": (139, 0, 0),
    "COLOR_PINK": (255, 192, 203),
    "COLOR_ORANGE": (255, 165, 0),
    "COLOR_GOLD": (255, 215, 0),
    "COLOR_YELLOW": (255, 255, 0),
    "COLOR_PURPLE": (128, 0, 128),
    "COLOR_MAGENTA": (255, 0, 255),
    "COLOR_LIME": (0, 255, 0),
    "COLOR_GREEN": (0, 128, 0),
    "COLOR_DARK_GREEN": (0, 100, 0),
    "COLOR_CYAN": (0, 255, 255),
    "COLOR_SKY_BLUE": (135, 206, 235),
    "COLOR_BLUE": (0, 0, 255),
    "COLOR_DARK_BLUE": (0, 0, 139),
    "COLOR_BROWN": (139, 69, 19),
}


class AssetManager:
    """
    资源管理器
    
    根据 config/eye/*.xml 配置加载虹膜、眼睑、背景等图像资源。
    """
    
    def __init__(self, config: Optional[EngineConfig] = None):
        """
        初始化资源管理器
        
        Args:
            config: 引擎配置，使用默认配置如果未指定
        """
        if not HAS_PIL:
            raise ImportError("需要 Pillow 库: pip install Pillow")
            
        self._config = config or EngineConfig()
        self._cache: Dict[str, Image.Image] = {}
        self._max_cache_size = 200
        
        # 加载 XML 配置
        self._loader = get_config_loader()

    @property
    def config_loader(self) -> EyeConfigLoader:
        """获取配置加载器"""
        return self._loader
    
    # ========================================================================
    # 虹膜加载
    # ========================================================================
    
    def load_iris_from_config(self, type_name: str, style: str, side: LcdSide = LcdSide.LEFT):
        """兼容旧接口的库调用 (返回 PIL 图像)"""
        img, _, _ = self.load_iris(type_name, style, side)
        return img

    def load_iris(self, type_name: str, style: str, side: LcdSide = LcdSide.LEFT):
        """
        加载虹膜图片 (使用 XML 配置)
        
        Args:
            type_name: 类型名称 (CLASSIC, MODERN, etc.)
            style: 样式名称 (COLOR_BLUE, APPLE, etc.)
            side: 左/右眼
            
        Returns:
            (PIL Image (RGBA), offset_x, offset_y)
            
        Raises:
            AssetNotFoundError: 图片不存在
        """
        # 兼容性处理: 如果 type_name 是小写，尝试转换为大写
        if type_name == "classic": type_name = "CLASSIC"
        if type_name == "modern": type_name = "MODERN"
        
        cache_key = f"iris:{type_name}:{style}:{side.name}"
        if cache_key in self._cache:
            iris_config = self._loader.get_iris_config(type_name, style)
            offset_x = iris_config.offset_x if iris_config else 0
            offset_y = iris_config.offset_y if iris_config else 0
            return self._cache[cache_key].copy(), offset_x, offset_y
        
        # 从配置获取路径
        iris_config = self._loader.get_iris_config(type_name, style)
        if iris_config is None:
            # 再次尝试不带前缀的 style (例如 COLOR_BLUE -> BLUE)
            if style.startswith("COLOR_"):
                iris_config = self._loader.get_iris_config(type_name, style[6:])
            
            if iris_config is None:
                raise AssetNotFoundError(f"虹膜配置不存在: {type_name}/{style}")
        
        # 选择左/右眼路径
        path_str = iris_config.left_path if side == LcdSide.LEFT else iris_config.right_path
        if not path_str:
            path_str = iris_config.left_path  # fallback
        
        # 处理路径 (配置文件中的路径以 /.doly 开头)
        path = self._resolve_path(path_str)
        
        img = self._load_image(path)
        self._cache[cache_key] = img
        
        return img.copy(), iris_config.offset_x, iris_config.offset_y

    def load_lid_from_config(self, lid_id: int) -> Optional[dict]:
        """
        加载眼睑配置和图像 (供渲染器使用)
        """
        img, fill = self.load_lid(lid_id)
        if img is None:
            return None
        return {
            'image': img,
            'fill_r': fill[0],
            'fill_g': fill[1],
            'fill_b': fill[2],
            'fill_a': fill[3]
        }

    def load_lid(self, lid_id: int):
        """
        加载眼睑图片 (使用 XML 配置)
        
        Args:
            lid_id: 眼睑 ID
            
        Returns:
            (PIL Image (RGBA), fill_color (r,g,b,a)) 或 (None, None) 如果 ID=0
        """
        if lid_id == 0:
            return None, None
            
        cache_key = f"lid:{lid_id}"
        if cache_key in self._cache:
            lid_config = self._loader.get_lid_image_config(lid_id)
            fill = (lid_config.fill_r, lid_config.fill_g, 
                    lid_config.fill_b, lid_config.fill_a) if lid_config else (0,0,0,0)
            return self._cache[cache_key].copy(), fill
        
        # 获取眼睑配置
        lid_config = self._loader.get_lid_image_config(lid_id)
        if lid_config is None:
            # 检查是否是动画眼睑
            if self._loader.is_lid_animation(lid_id):
                logger.debug(f"眼睑 ID {lid_id} 是动画，需要使用 SeqDecoder")
                return None, None
            logger.warning(f"眼睑配置不存在: {lid_id}")
            return None, None
        
        path = self._resolve_path(lid_config.path)
        
        if not path.exists():
            logger.warning(f"眼睑图片不存在: {path}")
            return None, None
        
        img = self._load_image(path)
        self._cache[cache_key] = img
        
        fill_color = (lid_config.fill_r, lid_config.fill_g, 
                      lid_config.fill_b, lid_config.fill_a)
        
        return img.copy(), fill_color
    
    def get_lid_animation_info(self, lid_id: int) -> Optional[LidAnimationConfig]:
        """获取眼睑动画配置"""
        return self._loader.get_lid_animation_config(lid_id)
    
    # ========================================================================
    # 背景加载
    # ========================================================================
    
    def load_background_from_config(self, type_name: str, style: str, side: LcdSide = LcdSide.LEFT):
        """兼容接口"""
        return self.load_background(type_name, style, side)
    
    def get_lid_animation_info(self, lid_id: int) -> Optional[LidAnimationConfig]:
        """获取眼睑动画配置"""
        return self._loader.get_lid_animation_config(lid_id)
    
    # ========================================================================
    # 背景加载
    # ========================================================================
    
    def load_background(self, type_name: str, style: str, side: LcdSide = LcdSide.LEFT):
        """
        加载背景 (使用 XML 配置)
        
        Args:
            type_name: 类型 (COLOR, IMAGE)
            style: 样式名称
            side: 左/右眼
            
        Returns:
            PIL Image (RGBA, 240x240)
        """
        cache_key = f"bg:{type_name}:{style}:{side.name}"
        if cache_key in self._cache:
            return self._cache[cache_key].copy()
        
        if type_name == "COLOR":
            # 纯色背景
            img = self._create_color_background(style)
        else:
            # 图片背景
            bg_config = self._loader.get_background_config(type_name, style)
            if bg_config is None:
                logger.warning(f"背景配置不存在: {type_name}/{style}")
                img = self.create_black_background()
            else:
                path_str = bg_config.left_path if side == LcdSide.LEFT else bg_config.right_path
                if not path_str:
                    path_str = bg_config.left_path
                
                path = self._resolve_path(path_str)
                if path.exists():
                    img = self._load_image(path)
                    # 确保是 240x240
                    if img.size != (LCD_WIDTH, LCD_HEIGHT):
                        img = img.resize((LCD_WIDTH, LCD_HEIGHT), Image.Resampling.LANCZOS)
                else:
                    logger.warning(f"背景图片不存在: {path}")
                    img = self.create_black_background()
        
        self._cache[cache_key] = img
        return img.copy()
    
    def _create_color_background(self, style: str):
        """创建纯色背景"""
        if style in COLOR_MAP:
            r, g, b = COLOR_MAP[style]
        else:
            # 尝试解析 COLOR_XXX 格式
            logger.warning(f"未知颜色: {style}，使用黑色")
            r, g, b = 0, 0, 0
        
        return Image.new('RGBA', (LCD_WIDTH, LCD_HEIGHT), (r, g, b, 255))
    
    def create_black_background(self):
        """创建黑色背景"""
        return Image.new('RGBA', (LCD_WIDTH, LCD_HEIGHT), (0, 0, 0, 255))
    
    def create_colored_background(self, r: int, g: int, b: int):
        """创建纯色背景"""
        return Image.new('RGBA', (LCD_WIDTH, LCD_HEIGHT), (r, g, b, 255))
    
    # ========================================================================
    # 通用方法
    # ========================================================================
    
    def load_image(self, path: str):
        """
        加载任意图片
        
        Args:
            path: 图片路径 (支持 /.doly 开头的配置路径)
            
        Returns:
            PIL Image (RGBA)
        """
        full_path = self._resolve_path(path)
        return self._load_image(full_path)
    
    def _resolve_path(self, path_str: str) -> Path:
        """
        解析路径 (统一使用 config 中的方法)
        """
        return self._config.resolve_asset_path(path_str)
    
    def _load_image(self, path: Path):
        """内部加载方法"""
        if not path.exists():
            raise AssetNotFoundError(f"图片不存在: {path}")
        
        try:
            img = Image.open(path)
            # 转换为 RGBA
            if img.mode != 'RGBA':
                img = img.convert('RGBA')
            return img
        except Exception as e:
            raise AssetNotFoundError(f"加载图片失败: {path}: {e}")
    
    # ========================================================================
    # 查询方法
    # ========================================================================
    
    def get_available_iris_types(self) -> List[str]:
        """获取可用的虹膜类型"""
        return self._loader.get_iris_types()
    
    def get_available_iris_styles(self, type_name: str) -> List[str]:
        """获取指定类型的虹膜样式"""
        return self._loader.get_iris_styles(type_name)
    
    def get_available_background_types(self) -> List[str]:
        """获取可用的背景类型"""
        return self._loader.get_background_types()
    
    def get_available_background_styles(self, type_name: str) -> List[str]:
        """获取指定类型的背景样式"""
        return self._loader.get_background_styles(type_name)
    
    def get_available_animations(self) -> List[str]:
        """获取所有可用的 XML 动画名称"""
        return self._loader.get_animation_names()
    
    def get_available_sequences(self) -> List[str]:
        """获取所有可用的 .seq 序列文件名称 (不含后缀)"""
        seq_path = self._config.animations_path
        if not seq_path.exists():
            return []
        return [f.stem for f in seq_path.glob("*.seq")]
    
    def get_available_lid_ids(self) -> List[int]:
        """获取所有可用的眼睑 ID"""
        return self._loader.get_all_lid_ids()
    
    # ========================================================================
    # 缓存管理
    # ========================================================================
    
    def clear_cache(self) -> None:
        """清除缓存"""
        self._cache.clear()
        logger.info("AssetManager: 缓存已清除")
    
    def get_cache_size(self) -> int:
        """获取缓存大小"""
        return len(self._cache)
    
    def preload_iris_type(self, type_name: str, side: LcdSide = LcdSide.LEFT) -> int:
        """
        预加载指定类型的所有虹膜
        
        Returns:
            预加载的图片数量
        """
        count = 0
        for style in self.get_available_iris_styles(type_name):
            try:
                self.load_iris(type_name, style, side)
                count += 1
            except AssetNotFoundError:
                pass
        logger.info(f"AssetManager: 预加载虹膜类型 {type_name}, {count} 张图片")
        return count

    def get_animations_by_category(self, category: str) -> List[str]:
        """获取指定分类的所有动画名称"""
        return [anim.name for anim in self._loader._eye_animations.values() 
                if category in anim.categories]

    def get_random_animation_for_category(self, category: str) -> Optional[str]:
        """随机获取指定分类下的一个动画名称"""
        import random
        anims = self.get_animations_by_category(category)
        if not anims:
            return None
        return random.choice(anims)

    def get_all_categories(self) -> List[str]:
        """获取所有可用的动画分类"""
        categories = set()
        for anim in self._loader._eye_animations.values():
            for cat in anim.categories:
                categories.add(cat)
        return sorted(list(categories))
