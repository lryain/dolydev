"""
序列播放器

负责播放 .seq 动画文件

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import logging
import threading
import time
from typing import Callable, Optional
import numpy as np

from ..config import LcdSide, EngineConfig
from ..constants import MIN_FPS, MAX_FPS
from ..drivers.interfaces import ILcdDriver
from .decoder import SeqDecoder

logger = logging.getLogger(__name__)


class SeqPlayer:
    """
    序列动画播放器
    
    在独立线程中播放 .seq 动画文件
    """
    
    def __init__(self, lcd_driver: ILcdDriver):
        """
        初始化播放器
        
        Args:
            lcd_driver: LCD 驱动
        """
        self._lcd_driver = lcd_driver
        self._decoder = SeqDecoder()
        
        # 播放状态
        self._playing = False
        self._paused = False
        self._loop = False
        self._loop_count = 0  # 0 or None means infinite if loop=True, or 1 if loop=False
        self._remaining_loops = 0
        # 默认 FPS 使用 EngineConfig 中的配置 (仅作为没传值时的后备)
        self._fps = EngineConfig().default_fps_seq
        self._current_frame = 0
        self._target_side = LcdSide.BOTH
        
        # 线程
        self._thread: Optional[threading.Thread] = None
        self._stop_event = threading.Event()
        
        # 回调
        self._on_complete: Optional[Callable[[], None]] = None
        self._on_frame: Optional[Callable[[int], None]] = None
        # frame consumer: callable(side: LcdSide, frame: numpy.ndarray[uint8])
        self._frame_consumer: Optional[Callable[[LcdSide, 'np.ndarray'], None]] = None
        
    def load(self, filepath: str, side: LcdSide = LcdSide.BOTH) -> bool:
        """
        加载动画文件
        
        Args:
            filepath: .seq 文件路径
            side: 目标 LCD
            
        Returns:
            成功返回 True
        """
        # 如果正在播放，先停止
        if self._playing:
            self.stop()
        
        try:
            self._decoder.load(filepath)
            self._target_side = side
            self._current_frame = 0
            logger.info(f"SeqPlayer: 加载 {filepath}, {self._decoder.get_frame_count()} 帧")
            return True
        except Exception as e:
            logger.error(f"SeqPlayer: 加载失败: {e}")
            return False
    
    def play(self, loop: bool = False, fps: Optional[int] = None, loop_count: int = None) -> None:
        """
        开始播放
        
        Args:
            loop: 是否循环
            fps: 帧率 (None 使用 EngineConfig.default_fps)
            loop_count: 循环次数 (仅在 loop=False 时有效，如果是 >0 则播放 N 次)
        """
        if not self._decoder.is_loaded():
            logger.warning("SeqPlayer: 未加载动画")
            return
        
        if self._playing:
            logger.warning("SeqPlayer: 已在播放中")
            return
        
        self._loop = loop
        self._loop_count = loop_count if loop_count is not None else 0
        self._remaining_loops = self._loop_count if self._loop_count > 0 else 0
        
        desired_fps = fps if fps is not None else EngineConfig().default_fps_seq
        self._fps = max(MIN_FPS, min(MAX_FPS, desired_fps))
        self._playing = True
        self._paused = False
        self._stop_event.clear()
        
        # 启动播放线程
        self._thread = threading.Thread(target=self._playback_loop, daemon=True)
        self._thread.start()
        
        logger.info(f"SeqPlayer: 开始播放, loop={loop}, fps={self._fps}")
    
    def _playback_loop(self) -> None:
        """播放线程主循环"""
        frame_duration = 1.0 / self._fps
        frame_count = self._decoder.get_frame_count()
        
        while self._playing and not self._stop_event.is_set():
            # 暂停状态
            if self._paused:
                time.sleep(0.01)
                continue
            
            start_time = time.perf_counter()
            
            try:
                # 获取并显示当前帧 (原始 RGBA bytes)
                # Use get_frame (RGBA) to preserve alpha channel; get_frame_rgb returns RGB over black
                frame_data = self._decoder.get_frame(self._current_frame)


                # frame_data is expected as bytes or similar from decoder; convert to numpy RGBA uint8
                try:
                    # decoder may return bytes or a PIL image; try to handle bytes->numpy
                    if isinstance(frame_data, (bytes, bytearray)):
                        # assume raw RGBA bytes (W*H*4) or RGB bytes (W*H*3)
                        arr = np.frombuffer(frame_data, dtype=np.uint8)
                        # decoder provides frames dimensions via get_dimensions() -> (width, height)
                        w, h = self._decoder.get_dimensions()
                        expected_rgba = h * w * 4
                        expected_rgb = h * w * 3
                        if arr.size == expected_rgba:
                            arr = arr.reshape((h, w, 4))
                        elif arr.size == expected_rgb:
                            # Received RGB bytes; expand to RGBA with full alpha
                            arr = arr.reshape((h, w, 3))
                            alpha = np.full((h, w, 1), 255, dtype=np.uint8)
                            arr = np.concatenate([arr, alpha], axis=2)
                        else:
                            raise ValueError(f"Unexpected frame byte size: {arr.size}, expected {expected_rgb} or {expected_rgba}")
                    else:
                        # if decoder returned a numpy array, PIL Image, or similar, try to normalize to RGBA numpy array
                        try:
                            from PIL import Image as _Image
                            if hasattr(frame_data, 'mode') and getattr(frame_data, 'mode'):
                                if getattr(frame_data, 'mode') != 'RGBA':
                                    frame_data = frame_data.convert('RGBA')
                        except Exception:
                            # Not a PIL image or conversion failed; proceed to attempt numpy conversion
                            pass
                        arr = np.array(frame_data, dtype=np.uint8)
                except Exception:
                    logger.exception("SeqPlayer: 无法将帧数据转换为 numpy 数组，回退到原始写入")
                    arr = None

                # 如果有 frame_consumer，交给上层处理合成并写入；否则回退到直接写入驱动（兼容）
                if self._frame_consumer and arr is not None:
                    try:
                        # logger.info(f"SeqPlayer: delivering frame to consumer side={self._target_side} shape={getattr(arr,'shape',None)}")
                        self._frame_consumer(self._target_side, arr)
                    except Exception:
                        logger.exception("SeqPlayer: frame_consumer 调用失败，丢弃当前帧")
                else:
                    # 兼容旧行为：直接写驱动（如果 decoder 提供 RGB bytes）
                    try:
                        logger.info("SeqPlayer: frame_consumer missing or arr is None, falling back to direct write")
                        self._lcd_driver.write_frame(frame_data)
                    except Exception:
                        logger.exception("SeqPlayer: 直接写驱动失败")
                
                # 帧回调
                if self._on_frame:
                    try:
                        self._on_frame(self._current_frame)
                    except Exception as e:
                        logger.error(f"SeqPlayer: 帧回调错误: {e}")
                
            except Exception as e:
                logger.error(f"SeqPlayer: 播放错误: {e}")
                self._playing = False
                break
            
            # 前进到下一帧
            self._current_frame += 1
            
            # 检查结束
            if self._current_frame >= frame_count:
                if self._loop:
                    self._current_frame = 0
                elif self._remaining_loops > 1:
                    self._remaining_loops -= 1
                    self._current_frame = 0
                    logger.info(f"SeqPlayer: 循环播放, 剩余次数: {self._remaining_loops}")
                else:
                    self._playing = False
                    if self._on_complete:
                        try:
                            self._on_complete()
                        except Exception as e:
                            logger.error(f"SeqPlayer: 完成回调错误: {e}")
                    break
            
            # 帧率控制
            elapsed = time.perf_counter() - start_time
            sleep_time = frame_duration - elapsed
            if sleep_time > 0:
                time.sleep(sleep_time)
        
        logger.info("SeqPlayer: 播放结束")
    
    def pause(self) -> None:
        """暂停播放"""
        if self._playing and not self._paused:
            self._paused = True
            logger.debug("SeqPlayer: 已暂停")
    
    def resume(self) -> None:
        """恢复播放"""
        if self._playing and self._paused:
            self._paused = False
            logger.debug("SeqPlayer: 已恢复")
    
    def stop(self) -> None:
        """停止播放"""
        if self._playing:
            self._playing = False
            self._stop_event.set()
            
            # 等待线程结束
            if self._thread and self._thread.is_alive():
                self._thread.join(timeout=1.0)
            
            self._thread = None
            self._current_frame = 0
            logger.debug("SeqPlayer: 已停止")
    
    def seek(self, frame_index: int) -> None:
        """
        跳转到指定帧
        
        Args:
            frame_index: 帧索引
        """
        if not self._decoder.is_loaded():
            return
            
        frame_count = self._decoder.get_frame_count()
        self._current_frame = max(0, min(frame_index, frame_count - 1))
        logger.debug(f"SeqPlayer: 跳转到帧 {self._current_frame}")
    
    @property
    def is_playing(self) -> bool:
        """检查是否正在播放"""
        return self._playing and not self._paused
    
    @property
    def is_paused(self) -> bool:
        """检查是否暂停"""
        return self._playing and self._paused
    
    def is_loaded(self) -> bool:
        """检查是否已加载"""
        return self._decoder.is_loaded()
    
    def get_progress(self) -> tuple:
        """
        获取播放进度
        
        Returns:
            (current_frame, total_frames)
        """
        return (self._current_frame, self._decoder.get_frame_count())
    
    def get_fps(self) -> int:
        """获取当前帧率"""
        return self._fps
    
    def set_fps(self, fps: int) -> None:
        """设置帧率"""
        self._fps = max(MIN_FPS, min(MAX_FPS, fps))
    
    def set_loop(self, loop: bool) -> None:
        """设置循环模式"""
        self._loop = loop
    
    def set_on_complete(self, callback: Optional[Callable[[], None]]) -> None:
        """
        设置播放完成回调
        
        Args:
            callback: 回调函数，无参数
        """
        self._on_complete = callback
    
    def set_on_frame(self, callback: Optional[Callable[[int], None]]) -> None:
        """
        设置帧回调
        
        Args:
            callback: 回调函数，参数为当前帧索引
        """
        self._on_frame = callback

    def set_frame_consumer(self, consumer: Optional[Callable[[LcdSide, 'np.ndarray'], None]]) -> None:
        """
        设置帧消费者。播放器在每帧解码后会把 numpy RGBA 帧传入消费者，由上层负责合成并统一写入。

        Args:
            consumer: Callable(side, frame_np) 或 None
        """
        self._frame_consumer = consumer
    
    def unload(self) -> None:
        """卸载动画，释放资源"""
        self.stop()
        self._decoder.unload()
    
    def __del__(self):
        """析构时停止播放"""
        self.stop()
