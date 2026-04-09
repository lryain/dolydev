"""
LCD 硬件驱动 (ctypes 实现)

通过 ctypes 调用 libLcdControl.so 库与硬件交互。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import ctypes
from ctypes import c_bool, c_int8, c_uint8, c_int, c_void_p, POINTER, Structure, byref, cast
import logging
import threading
from pathlib import Path
from typing import Optional

from ..config import LcdSide, EngineConfig
from ..constants import LCD_WIDTH, LCD_HEIGHT
from ..exceptions import LcdDriverError
from .interfaces import ILcdDriver

logger = logging.getLogger(__name__)


class LcdData(Structure):
    """
    对应 C++ LcdData 结构
    
    struct LcdData {
        uint8_t side;
        uint8_t* buffer;
    };
    """
    _fields_ = [
        ("side", c_uint8),
        ("buffer", POINTER(c_uint8)),
    ]


class LcdDriver(ILcdDriver):
    """
    LCD 硬件驱动
    
    通过 ctypes 调用 C++ libLcdControl.so 库。
    
    Note:
        此驱动需要在树莓派上运行，且需要相应的硬件支持。
    """
    
    # 硬件互斥锁，防止多线程竞争 /dev/doly_lcd
    _global_lock = threading.Lock()

    # 颜色深度常量
    LCD_12BIT = 0x03
    # LCD_18BIT = 0x06
    
    # 类级别的共享库实例 (单例模式)
    _shared_lib = None
    _shared_init_count = 0
    _f_init = None
    _f_release = None
    _f_get_buffer_size = None
    _f_write_lcd = None
    _f_color_fill = None
    _f_buffer_from_rgb = None
    _f_buffer_from_rgb_uses_mirror_flag = False

    def __init__(self, lib_path: Optional[str] = None, side: LcdSide = LcdSide.LEFT):
        """
        初始化 LCD 驱动
        
        Args:
            lib_path: LCD 库路径，默认自动查找
            side: LCD 侧别 (LEFT 或 RIGHT)
        """
        self._lib_path = lib_path or self._find_lib_path()
        self._side = side
        self._active = False
        self._buffer = None
        self._rgb_buffer = None
        self._buffer_size = 0
        self._color_depth = self.LCD_12BIT
        
    def _find_lib_path(self) -> str:
        """查找 LCD 库路径"""
        # 统一使用 EngineConfig 中的定义的路径
        lib_path = EngineConfig().lcd_lib_path
        if Path(lib_path).exists():
            return lib_path
            
        # 备选路径
        possible_paths = [
            "/usr/local/lib/libLcdControl.so",
        ]
        
        for path in possible_paths:
            if Path(path).exists():
                return path
                
        return lib_path
    
    @property
    def side(self) -> LcdSide:
        """获取 LCD 侧别"""
        return self._side
    
    @property
    def is_active(self) -> bool:
        """检查是否已初始化"""
        return self._active
    
    def init(self) -> bool:
        """
        初始化 LCD 硬件
        
        Returns:
            成功返回 True
            
        Raises:
            LcdDriverError: 库加载或初始化失败
        """
        if self._active:
            return True
            
        try:
            # 加载共享库 (单例处理)
            if LcdDriver._shared_lib is None:
                logger.info(f"LcdDriver: 加载库 {self._lib_path}")
                
                if not Path(self._lib_path).exists():
                    raise LcdDriverError(f"LCD 库不存在: {self._lib_path}")
                
                LcdDriver._shared_lib = ctypes.CDLL(self._lib_path)
            
            # 确保函数已设置 (无论库是否已加载)
            self._setup_functions()
            
            # 调用初始化 (全局仅一次)
            # LcdDriver._shared_init_count = 1
            if LcdDriver._shared_init_count == 0:
                result = LcdDriver._f_init(self._color_depth)
                if result < 0:
                    raise LcdDriverError(f"LCD 初始化失败，错误码: {result}")
            
            LcdDriver._shared_init_count += 1
            
            # 分配缓冲区
            self._buffer_size = LcdDriver._f_get_buffer_size()
            self._buffer = (c_uint8 * self._buffer_size)()
            self._rgb_buffer = (c_uint8 * (LCD_WIDTH * LCD_HEIGHT * 3))()
            
            self._active = True
            logger.info(f"LcdDriver: 初始化成功 (side={self._side.name}, "
                       f"buffer_size={self._buffer_size})")
            return True
            
        except OSError as e:
            raise LcdDriverError(f"无法加载 LCD 库: {e}")
        except Exception as e:
            raise LcdDriverError(f"LCD 初始化失败: {e}")

    @staticmethod
    def _get_symbol(lib, *candidates):
        """兼容不同版本 libLcdControl.so 的导出符号。"""
        last_error = None
        for name in candidates:
            try:
                return getattr(lib, name)
            except AttributeError as exc:
                last_error = exc
        if last_error is None:
            raise AttributeError("未提供可解析的符号名")
        raise last_error
    
    def _setup_functions(self) -> None:
        """设置 C 函数签名 (处理 C++ mangled 符号)"""
        if LcdDriver._f_init is not None:
            return

        lib = LcdDriver._shared_lib
        
        try:
            # 定义核心符号映射 (基于 nm -D libLcdControl.so 的结果)
            # 对应 LcdControl::init(LcdColorDepth)
            LcdDriver._f_init = getattr(lib, "_ZN10LcdControl4initE13LcdColorDepth")
            LcdDriver._f_init.argtypes = [c_uint8]
            LcdDriver._f_init.restype = c_int8
            
            # 兼容旧版 LcdControl::release() 与新版 LcdControl::dispose()
            LcdDriver._f_release = self._get_symbol(
                lib,
                "_ZN10LcdControl7releaseEv",
                "_ZN10LcdControl7disposeEv",
            )
            LcdDriver._f_release.argtypes = []
            LcdDriver._f_release.restype = c_int8
            
            # 对应 LcdControl::getBufferSize()
            LcdDriver._f_get_buffer_size = getattr(lib, "_ZN10LcdControl13getBufferSizeEv")
            LcdDriver._f_get_buffer_size.argtypes = []
            LcdDriver._f_get_buffer_size.restype = c_int
            
            # 对应 LcdControl::writeLcd(LcdData*)
            LcdDriver._f_write_lcd = getattr(lib, "_ZN10LcdControl8writeLcdEP7LcdData")
            LcdDriver._f_write_lcd.argtypes = [POINTER(LcdData)]
            LcdDriver._f_write_lcd.restype = c_int8
            
            # 对应 LcdControl::LcdColorFill(LcdSide, uint8_t r, uint8_t g, uint8_t b)
            LcdDriver._f_color_fill = getattr(lib, "_ZN10LcdControl12LcdColorFillE7LcdSidehhh")
            LcdDriver._f_color_fill.argtypes = [c_uint8, c_uint8, c_uint8, c_uint8]
            LcdDriver._f_color_fill.restype = None

            # 兼容旧版 LcdBufferFrom24Bit(output, input) 与新版 toLcdBuffer(output, input, mirror)
            try:
                LcdDriver._f_buffer_from_rgb = getattr(lib, "_ZN10LcdControl18LcdBufferFrom24BitEPhS0_")
                LcdDriver._f_buffer_from_rgb.argtypes = [POINTER(c_uint8), POINTER(c_uint8)]
                LcdDriver._f_buffer_from_rgb_uses_mirror_flag = False
            except AttributeError:
                LcdDriver._f_buffer_from_rgb = getattr(lib, "_ZN10LcdControl11toLcdBufferEPhS0_b")
                LcdDriver._f_buffer_from_rgb.argtypes = [POINTER(c_uint8), POINTER(c_uint8), c_bool]
                LcdDriver._f_buffer_from_rgb_uses_mirror_flag = True
            LcdDriver._f_buffer_from_rgb.restype = None

        except AttributeError as e:
            logger.error(f"LcdDriver: 库中缺少必要符号: {e}")
            raise LcdDriverError(f"LCD 库版本不匹配或损坏: {e}")

    def release(self) -> None:
        """释放资源"""
        if not self._active:
            return
            
        LcdDriver._shared_init_count -= 1
        if LcdDriver._shared_init_count <= 0:
            if LcdDriver._shared_lib:
                LcdDriver._f_release()
                LcdDriver._shared_lib = None
                LcdDriver._f_init = None
                LcdDriver._f_release = None
                LcdDriver._f_get_buffer_size = None
                LcdDriver._f_write_lcd = None
                LcdDriver._f_color_fill = None
                
        self._active = False
        self._buffer = None
        logger.info(f"LcdDriver: 释放资源 ({self._side.name})")

    def write_frame(self, frame_data: bytes) -> bool:
        """
        写入一帧图像
        
        Args:
            frame_data: RGB888 格式的图像数据
            
        Returns:
            成功返回 True
        """
        if not self._active:
            return False
            
        try:
            # 1. 将 Python bytes 复制到 RGB 缓冲区
            try:
                import hashlib as _hashlib
                h = _hashlib.md5(frame_data).hexdigest()
                # logger.info(f"LcdDriver.write_frame: side={self._side.name} md5={h} len={len(frame_data)}")
            except Exception:
                logger.exception("LcdDriver.write_frame: md5 compute failed")
            ctypes.memmove(self._rgb_buffer, frame_data, min(len(frame_data), len(self._rgb_buffer)))
            
            # 2. 调用库函数将 RGB888 转换为 LCD 专用格式 (12bit/18bit)
            if LcdDriver._f_buffer_from_rgb_uses_mirror_flag:
                LcdDriver._f_buffer_from_rgb(self._buffer, self._rgb_buffer, False)
            else:
                LcdDriver._f_buffer_from_rgb(self._buffer, self._rgb_buffer)
            
            # 3. 准备 LcdData 结构并写入 LCD
            side_val = 0 if self._side == LcdSide.LEFT else 1
            
            data = LcdData()
            data.side = side_val
            data.buffer = cast(self._buffer, POINTER(c_uint8))
            
            with self._global_lock:
                result = LcdDriver._f_write_lcd(byref(data))
            return result == 0
            
        except Exception as e:
            logger.error(f"LCD 写入异常: {e}")
            return False

    def write(self, data: bytes) -> bool:
        """
        兼容性别名，等同于 write_frame
        """
        return self.write_frame(data)

    def fill_color(self, *args) -> bool:
        """
        填充纯色
        
        用法:
            fill_color(r, g, b)
            fill_color(side, r, g, b)  # 兼容测试脚本
        """
        if not self._active:
            return False
            
        if len(args) >= 4:
            # 兼容 (side, r, g, b) 或 (side, r, g, b, unused)
            _, r, g, b = args[:4]
        elif len(args) == 3:
            r, g, b = args
        else:
            logger.error(f"fill_color 参数错误: {args}")
            return False
        
        try:
            side_val = 0 if self._side == LcdSide.LEFT else 1
            print(f"-----> Filling color on side {side_val}: R={r}, G={g}, B={b}")
            LcdDriver._f_color_fill(side_val, int(r), int(g), int(b))
            return True
        except Exception as e:
            logger.error(f"LCD 填充失败: {e}")
            return False

    def release(self) -> None:
        """释放资源"""
        if not self._active:
            return
            
        LcdDriver._shared_init_count -= 1
        if LcdDriver._shared_init_count <= 0:
            if LcdDriver._shared_lib and LcdDriver._f_release:
                try:
                    LcdDriver._f_release()
                    logger.info("LcdDriver: 硬件驱动已关闭")
                except Exception as e:
                    logger.error(f"LcdDriver: 释放失败: {e}")
                finally:
                    LcdDriver._shared_lib = None
                    LcdDriver._f_init = None
                    LcdDriver._f_release = None
                    LcdDriver._f_get_buffer_size = None
                    LcdDriver._f_write_lcd = None
                    LcdDriver._f_color_fill = None
                    LcdDriver._f_buffer_from_rgb = None
                    LcdDriver._f_buffer_from_rgb_uses_mirror_flag = False
                
        self._active = False
        self._buffer = None
        self._rgb_buffer = None
        logger.info(f"LcdDriver: 释放资源 ({self._side.name})")
    
    def set_brightness(self, level: int) -> bool:
        """
        设置亮度
        
        Args:
            level: 亮度级别 (0-10)
            
        Returns:
            成功返回 True
        """
        if not self._active:
            raise LcdDriverError("LCD 未初始化")
        
        try:
            # 查找 setBrightness 符号 (可能已 mangled)
            # Doly 固件中的 setBrightness 通常不依赖 side
            if not hasattr(LcdDriver, "_f_set_brightness"):
                lib = LcdDriver._shared_lib
                try:
                    # _ZN10LcdControl13setBrightnessEh
                    LcdDriver._f_set_brightness = getattr(lib, "_ZN10LcdControl13setBrightnessEh")
                    LcdDriver._f_set_brightness.argtypes = [c_uint8]
                    LcdDriver._f_set_brightness.restype = c_int8
                except AttributeError:
                    logger.warning("LcdDriver: 未找到 setBrightness 符号")
                    return False
            
            level = max(0, min(10, level))
            result = LcdDriver._f_set_brightness(c_uint8(level))
            return result >= 0
        except Exception as e:
            logger.error(f"设置亮度失败: {e}")
            return False


class DualLcdDriver:
    """
    双 LCD 驱动
    
    管理左右两个 LCD 屏幕，简化操作。
    """
    
    def __init__(self, lib_path: Optional[str] = None):
        """
        初始化双 LCD 驱动
        
        Args:
            lib_path: LCD 库路径
        """
        self._lib_path = lib_path
        self._left = LcdDriver(lib_path, LcdSide.LEFT)
        self._right = LcdDriver(lib_path, LcdSide.RIGHT)
        self._active = False
    
    @property
    def left(self) -> LcdDriver:
        """左眼 LCD"""
        return self._left
    
    @property
    def right(self) -> LcdDriver:
        """右眼 LCD"""
        return self._right
    
    def init(self) -> bool:
        """初始化两个 LCD"""
        if self._active:
            return True
        
        # 初始化左眼 (会初始化共享库)
        if not self._left.init():
            return False
        
        # 初始化右眼 (复用共享库)
        if not self._right.init():
            self._left.release()
            return False
        
        self._active = True
        return True
    
    def release(self) -> None:
        """释放两个 LCD"""
        if self._active:
            self._right.release()
            self._left.release()
            self._active = False
    
    def fill_both(self, r: int, g: int, b: int) -> bool:
        """同时填充两个 LCD"""
        return self._left.fill_color(r, g, b) and self._right.fill_color(r, g, b)
    
    def write_frames(self, left_frame: bytes, right_frame: bytes) -> bool:
        """同时写入两个画面的帧数据"""
        res_l = self._left.write_frame(left_frame)
        res_r = self._right.write_frame(right_frame)
        return res_l and res_r

    def set_brightness(self, level: int) -> bool:
        """设置亮度 (影响两个 LCD)"""
        return self._left.set_brightness(level)
    
    def __enter__(self):
        self.init()
        return self
    
    def __exit__(self, *args):
        self.release()


class DualLcdDriver:
    """
    双 LCD 驱动
    
    管理左右两个 LCD 屏幕，简化操作。
    """
    
    def __init__(self, lib_path: Optional[str] = None):
        """
        初始化双 LCD 驱动
        
        Args:
            lib_path: LCD 库路径
        """
        self._lib_path = lib_path
        self._left = LcdDriver(lib_path, LcdSide.LEFT)
        self._right = LcdDriver(lib_path, LcdSide.RIGHT)
        self._active = False
    
    @property
    def left(self) -> LcdDriver:
        """左眼 LCD"""
        return self._left
    
    @property
    def right(self) -> LcdDriver:
        """右眼 LCD"""
        return self._right
    
    def init(self) -> bool:
        """初始化两个 LCD"""
        if self._active:
            return True
        
        # 初始化左眼 (会初始化共享库)
        if not self._left.init():
            return False
        
        # 初始化右眼 (复用共享库)
        if not self._right.init():
            self._left.release()
            return False
        
        self._active = True
        return True
    
    def release(self) -> None:
        """释放两个 LCD"""
        if self._active:
            self._right.release()
            self._left.release()
            self._active = False
    
    def fill_both(self, r: int, g: int, b: int) -> bool:
        """同时填充两个 LCD"""
        return self._left.fill_color(r, g, b) and self._right.fill_color(r, g, b)
    
    def set_brightness(self, level: int) -> bool:
        """设置亮度 (影响两个 LCD)"""
        return self._left.set_brightness(level)
    
    def __enter__(self):
        self.init()
        return self
    
    def __exit__(self, *args):
        self.release()
