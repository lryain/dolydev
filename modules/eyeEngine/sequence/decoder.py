"""
序列文件解码器

解析 Doly .seq 动画文件格式

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
from pathlib import Path
from typing import List, Optional, Tuple

try:
    import lz4.frame
    HAS_LZ4 = True
except ImportError:
    HAS_LZ4 = False

try:
    from PIL import Image
    HAS_PIL = True
except ImportError:
    HAS_PIL = False

from ..constants import LCD_WIDTH, LCD_HEIGHT, FRAME_SIZE, LZ4F_MAGIC
from ..exceptions import SeqDecodeError

logger = logging.getLogger(__name__)


class SeqDecoder:
    """
    .seq 动画文件解码器
    
    .seq 文件格式:
    - LZ4 Frame 压缩 (Magic: 0x184D2204)
    - 解压后为连续的 RGBA8888 帧数据
    - 每帧: 240 x 240 x 4 = 230,400 bytes
    """
    
    def __init__(self, width: int = LCD_WIDTH, height: int = LCD_HEIGHT):
        """
        初始化解码器
        
        Args:
            width: 帧宽度
            height: 帧高度
        """
        if not HAS_LZ4:
            raise ImportError("需要 lz4 库: pip install lz4")
            
        self._width = width
        self._height = height
        self._bpp = 4  # RGBA8888
        self._frame_size = width * height * self._bpp
        
        self._filepath: Optional[Path] = None
        self._frames: List[bytes] = []
        self._frame_count = 0
        self._loaded = False
        
    def load(self, filepath: str) -> bool:
        """
        加载并解析 .seq 文件
        
        Args:
            filepath: .seq 文件路径
            
        Returns:
            成功返回 True
            
        Raises:
            SeqDecodeError: 解码失败
        """
        self._filepath = Path(filepath)
        
        if not self._filepath.exists():
            raise SeqDecodeError(f"文件不存在: {filepath}")
        
        try:
            # 读取压缩数据
            with open(self._filepath, 'rb') as f:
                compressed_data = f.read()
            
            # 验证 LZ4 magic number
            if len(compressed_data) < 4:
                raise SeqDecodeError("文件太小")
            
            magic = int.from_bytes(compressed_data[:4], 'little')
            if magic != LZ4F_MAGIC:
                raise SeqDecodeError(f"不是有效的 LZ4 Frame 文件, magic: {hex(magic)}")
            
            # 解压
            logger.debug(f"SeqDecoder: 解压 {self._filepath.name}")
            decompressed = lz4.frame.decompress(compressed_data)
            
            # 计算帧数
            total_size = len(decompressed)
            self._frame_count = total_size // self._frame_size
            
            if total_size % self._frame_size != 0:
                logger.warning(
                    f"SeqDecoder: 数据大小 {total_size} 不是帧大小 {self._frame_size} 的整数倍"
                )
            
            # 分割帧
            self._frames = []
            for i in range(self._frame_count):
                start = i * self._frame_size
                end = start + self._frame_size
                self._frames.append(decompressed[start:end])
            
            self._loaded = True
            logger.info(
                f"SeqDecoder: 加载成功 {self._filepath.name}, "
                f"{self._frame_count} 帧, {total_size} bytes"
            )
            return True
            
        except lz4.frame.LZ4FrameError as e:
            raise SeqDecodeError(f"LZ4 解压失败: {e}")
        except Exception as e:
            raise SeqDecodeError(f"加载失败: {e}")
    
    def get_frame(self, index: int) -> bytes:
        """
        获取指定帧的原始数据
        
        Args:
            index: 帧索引 (0-based)
            
        Returns:
            RGBA8888 格式帧数据
            
        Raises:
            IndexError: 索引超出范围
            SeqDecodeError: 未加载
        """
        if not self._loaded:
            raise SeqDecodeError("文件未加载")
        
        if index < 0 or index >= self._frame_count:
            raise IndexError(f"帧索引超出范围: {index}, 总帧数: {self._frame_count}")
        
        return self._frames[index]
    
    def get_frame_image(self, index: int) -> 'Image.Image':
        """
        获取指定帧的 PIL Image
        
        Args:
            index: 帧索引 (0-based)
            
        Returns:
            PIL Image (RGBA)
        """
        if not HAS_PIL:
            raise ImportError("需要 Pillow 库: pip install Pillow")
        
        frame_data = self.get_frame(index)
        return Image.frombytes('RGBA', (self._width, self._height), frame_data)
    
    def get_frame_rgb(self, index: int) -> bytes:
        """
        获取指定帧的 RGB888 数据 (处理透明度)
        
        将 RGBA 帧与黑色背景合成，以正确处理透明动画。
        
        Args:
            index: 帧索引
            
        Returns:
            RGB888 格式帧数据
        """
        rgba_data = self.get_frame(index)
        
        # 优先使用 PIL 进行转换 (支持高质量 Alpha 合成)
        if HAS_PIL:
            # 使用 frombuffer 加载 RGBA
            img = Image.frombuffer("RGBA", (self._width, self._height), rgba_data, "raw", "RGBA", 0, 1)
            
            # 创建黑色背景图片
            background = Image.new("RGB", (self._width, self._height), (0, 0, 0))
            # 使用自身的 Alpha 通道进行合成
            background.paste(img, (0, 0), img)
            
            return background.tobytes()
        
        # 备选方案: 手动 Alpha 合成 (RGBA -> RGB over Black)
        rgb_size = self._width * self._height * 3
        rgb_bytes = bytearray(rgb_size)
        
        for i in range(self._width * self._height):
            src_idx = i * 4
            dst_idx = i * 3
            
            r = rgba_data[src_idx]
            g = rgba_data[src_idx + 1]
            b = rgba_data[src_idx + 2]
            a = rgba_data[src_idx + 3]
            
            if a == 255:
                rgb_bytes[dst_idx] = r
                rgb_bytes[dst_idx + 1] = g
                rgb_bytes[dst_idx + 2] = b
            elif a == 0:
                rgb_bytes[dst_idx] = 0
                rgb_bytes[dst_idx + 1] = 0
                rgb_bytes[dst_idx + 2] = 0
            else:
                # 线性合成
                alpha = a / 255.0
                rgb_bytes[dst_idx] = int(r * alpha)
                rgb_bytes[dst_idx + 1] = int(g * alpha)
                rgb_bytes[dst_idx + 2] = int(b * alpha)
        
        return bytes(rgb_bytes)
    
    def get_frame_count(self) -> int:
        """获取总帧数"""
        return self._frame_count
    
    def get_dimensions(self) -> Tuple[int, int]:
        """获取帧尺寸"""
        return (self._width, self._height)
    
    def is_loaded(self) -> bool:
        """检查是否已加载"""
        return self._loaded
    
    def get_filepath(self) -> Optional[str]:
        """获取文件路径"""
        return str(self._filepath) if self._filepath else None
    
    def unload(self) -> None:
        """卸载数据，释放内存"""
        self._frames.clear()
        self._frame_count = 0
        self._loaded = False
        self._filepath = None
        logger.debug("SeqDecoder: 已卸载")
    
    def get_memory_usage(self) -> int:
        """估算内存占用 (bytes)"""
        if not self._loaded:
            return 0
        return self._frame_count * self._frame_size
    
    def __len__(self) -> int:
        """返回帧数"""
        return self._frame_count
    
    def __getitem__(self, index: int) -> bytes:
        """支持下标访问"""
        return self.get_frame(index)
    
    def __iter__(self):
        """支持迭代"""
        for i in range(self._frame_count):
            yield self._frames[i]
