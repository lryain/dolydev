"""
眼睛渲染器

负责合成多层图片生成眼睛图像

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from __future__ import annotations

import logging
from typing import List, Optional, Tuple, TYPE_CHECKING

if TYPE_CHECKING:
    from PIL import Image as PILImage

try:
    from PIL import Image, ImageDraw
    HAS_PIL = True
    # 兼容低版本 Pillow
    if not hasattr(Image, 'Resampling'):
        Image.Resampling = Image
except ImportError:
    HAS_PIL = False

from .config import EyeState, EngineConfig, LcdSide
from .constants import (
    LCD_WIDTH, LCD_HEIGHT, IRIS_WIDTH, IRIS_HEIGHT,
    IRIS_MAX_OFFSET_X, IRIS_MAX_OFFSET_Y, LID_SIDE_IDS, LID_VERTICAL_IDS
)
from .assets import AssetManager
from .exceptions import AssetNotFoundError

logger = logging.getLogger(__name__)


class EyeRenderer:
    """
    眼睛渲染器
    
    将虹膜、眼睑、背景等图层合成为最终眼睛图像
    
    图层顺序 (从下到上):
    1. 背景层 (background)
    2. 虹膜层 (iris) - 可偏移
    3. 眼睑层 (lid)
    4. 圆形遮罩 (circular mask)
    """
    
    def __init__(self, asset_manager: Optional[AssetManager] = None,
                 config: Optional[EngineConfig] = None):
        """
        初始化渲染器
        
        Args:
            asset_manager: 资源管理器
            config: 引擎配置
        """
        if not HAS_PIL:
            raise ImportError("需要 Pillow 库: pip install Pillow")
        
        self._config = config or EngineConfig()
        self._asset_manager = asset_manager or AssetManager(self._config)
        
        self._width = LCD_WIDTH
        self._height = LCD_HEIGHT
        
        # 创建圆形遮罩
        self._circular_mask = self._create_circular_mask()
        
    def _create_circular_mask(self):
        """创建圆形遮罩"""
        mask = Image.new('L', (self._width, self._height), 0)
        draw = ImageDraw.Draw(mask)
        draw.ellipse([0, 0, self._width - 1, self._height - 1], fill=255)
        return mask
    
    def render(self, state: EyeState, side: LcdSide) -> PILImage.Image:
        """
        渲染眼睛 (render_eye 的别名，便于引擎调用)
        """
        return self.render_eye(state, side)

    def render_eye(self, state: EyeState, side: LcdSide) -> PILImage.Image:
        """
        渲染眼睛
        
        Args:
            state: 眼睛状态
            side: 左/右眼
            
        Returns:
            合成后的 RGBA 图像 (240x240)
        """
        # 1. 背景层
        try:
            background = self._asset_manager.load_background_from_config(
                state.background_type, state.background_style
            )
        except Exception:
            background = self._asset_manager.create_black_background()
            
        # 创建画布
        canvas = Image.new('RGBA', (self._width, self._height), (0, 0, 0, 255))
        canvas.paste(background, (0, 0))
        
        # 2. 虹膜层
        try:
            iris = self._asset_manager.load_iris_from_config(
                state.iris_theme, state.iris_style, side
            )
            
            # 处理缩放和旋转
            if state.scale_x != 1.0 or state.scale_y != 1.0:
                new_w = int(iris.width * state.scale_x)
                new_h = int(iris.height * state.scale_y)
                if new_w > 0 and new_h > 0:
                    iris = iris.resize((new_w, new_h), Image.Resampling.LANCZOS)
            
            if state.rotation != 0:
                iris = iris.rotate(state.rotation, expand=True, resample=Image.Resampling.BICUBIC)
            
            # 计算位置 (state.iris_x, state.iris_y 为中心坐标)
            x = state.iris_x - (iris.width // 2)
            y = state.iris_y - (iris.height // 2)
            
            # 合成虹膜
            canvas.paste(iris, (x, y), iris)
        except Exception as e:
            logger.warning(f"渲染虹膜失败: {e}")
        
        # 3. 眼睑层
        # 我们按照先渲染背景填充，再渲染眼睑图片的逻辑（如果配置中有 fill_color）
        
        # 上眼睑
        if state.top_lid_id > 0:
            self._render_lid(canvas, state.top_lid_id, state.top_lid_y, "top", side)
            
        # 下眼睑
        if state.bottom_lid_id > 0:
            self._render_lid(canvas, state.bottom_lid_id, state.bottom_lid_y, "bottom", side)
        
        # 4. 应用圆形遮罩
        result = Image.new('RGBA', (self._width, self._height), (0, 0, 0, 255))
        result.paste(canvas, (0, 0), self._circular_mask)
        
        return result

    def _render_lid(self, canvas: Image.Image, lid_id: int, y_offset: int, lid_type: str, side: LcdSide):
        """
        渲染单层眼睑
        """
        if lid_id == 0:
            return
            
        try:
            lid_data = self._asset_manager.load_lid(lid_id)
            if not lid_data or lid_data[0] is None:
                # 可能是动画眼睑
                lid_info = self._asset_manager.get_lid_animation_info(lid_id)
                if lid_info:
                    # 动画眼睑逻辑... (目前暂不支持完整动画，仅取基本值)
                    return
                return
            
            lid_img, fill_color = lid_data
            
            # 如果有填充颜色，先在眼睑区域填充
            if fill_color and len(fill_color) == 4 and fill_color[3] > 0:
                draw = ImageDraw.Draw(canvas)
                if lid_type == "top":
                    # 填充上方区域
                    if y_offset > 0:
                        draw.rectangle([0, 0, self._width, y_offset], fill=fill_color)
                else:
                    # 填充下方区域
                    fill_start_y = y_offset + lid_img.height
                    if fill_start_y < self._height:
                        draw.rectangle([0, fill_start_y, self._width, self._height], fill=fill_color)
            
            # 合成眼睑图片
            img_x = (self._width - lid_img.width) // 2
            img_y = y_offset
            canvas.paste(lid_img, (img_x, img_y), lid_img)
            
        except Exception as e:
            logger.warning(f"渲染眼睑 {lid_id} 失败: {e}")

    def _calculate_iris_position(self, iris_x: int, iris_y: int, iris_w: int, iris_h: int) -> Tuple[int, int]:
        """已废弃，直接使用 state.iris_x/y"""
        return (iris_x - iris_w // 2, iris_y - iris_h // 2)
    
    def _calculate_lid_position(self, lid, lid_type: str) -> Tuple[int, int]:
        """
        计算眼睑位置
        
        Args:
            lid: 眼睑图像
            lid_type: "side", "top", "bottom"
            
        Returns:
            (x, y) 像素坐标
        """
        lid_w, lid_h = lid.size
        
        if lid_type == "side":
            # 侧眼睑: 水平居中，垂直靠上
            x = (self._width - lid_w) // 2
            y = 0
        elif lid_type == "top":
            # 上眼睑: 水平居中，垂直靠上
            x = (self._width - lid_w) // 2
            y = 0
        elif lid_type == "bottom":
            # 下眼睑: 水平居中，垂直靠下
            x = (self._width - lid_w) // 2
            y = self._height - lid_h
        else:
            x = (self._width - lid_w) // 2
            y = (self._height - lid_h) // 2
            
        return (x, y)
    
    def compose_layers(self, layers: List[Tuple]):
        """
        合成多个图层
        
        Args:
            layers: [(image, position), ...] 从底层到顶层
            
        Returns:
            合成后的 RGBA 图像
        """
        # 创建画布
        canvas = Image.new('RGBA', (self._width, self._height), (0, 0, 0, 255))
        
        for img, pos in layers:
            if img is None:
                continue
                
            # 确保是 RGBA
            if img.mode != 'RGBA':
                img = img.convert('RGBA')
            
            # 使用 alpha 通道作为遮罩进行合成
            canvas.paste(img, pos, img)
        
        return canvas
    
    def apply_circular_mask(self, image):
        """
        应用圆形遮罩
        
        Args:
            image: 输入图像 (RGBA)
            
        Returns:
            应用遮罩后的图像
        """
        # 创建输出图像
        result = Image.new('RGBA', (self._width, self._height), (0, 0, 0, 255))
        
        # 应用遮罩
        result.paste(image, (0, 0), self._circular_mask)
        
        return result
    
    def convert_to_rgb888(self, image) -> bytes:
        """
        将 PIL Image 转换为 RGB888 字节数据
        
        Args:
            image: RGBA 图像
            
        Returns:
            RGB888 字节数据 (172800 bytes)
        """
        if image.mode == 'RGBA':
            # 创建黑色背景
            rgb_image = Image.new('RGB', image.size, (0, 0, 0))
            # 使用 alpha 通道合成
            rgb_image.paste(image, mask=image.split()[3])
            image = rgb_image
        elif image.mode != 'RGB':
            image = image.convert('RGB')
        
        return image.tobytes()
    
    def convert_to_rgba8888(self, image) -> bytes:
        """
        将 PIL Image 转换为 RGBA8888 字节数据
        
        Args:
            image: 图像
            
        Returns:
            RGBA8888 字节数据 (230400 bytes)
        """
        if image.mode != 'RGBA':
            image = image.convert('RGBA')
        return image.tobytes()
    
    def render_to_rgb888(self, state: EyeState, side: LcdSide) -> bytes:
        """
        渲染并转换为 RGB888
        
        Args:
            state: 眼睛状态
            side: 左/右眼
            
        Returns:
            RGB888 字节数据
        """
        image = self.render_eye(state, side)
        return self.convert_to_rgb888(image)
    
    def create_blink_frames(self, state: EyeState, side: LcdSide, 
                            num_frames: int = 5) -> List[bytes]:
        """
        创建眨眼动画帧
        
        Args:
            state: 基础眼睛状态
            side: 左/右眼
            num_frames: 帧数
            
        Returns:
            RGB888 帧数据列表 (关闭 -> 打开)
        """
        frames = []
        
        # 使用不同的眼睑 Y 偏移模拟眨眼
        # 假设 y_offset 越大眼睛越闭合
        offsets = [20, 40, 60, 40, 20][:num_frames]
        
        for offset in offsets:
            blink_state = state.copy()
            blink_state.top_lid_y = offset
            blink_state.bottom_lid_y = 240 - offset
            frame = self.render_to_rgb888(blink_state, side)
            frames.append(frame)
        
        return frames
