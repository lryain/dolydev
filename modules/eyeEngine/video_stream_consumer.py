"""
FaceReco 视频流消费者（Python 实现）

通过 System V 共享内存 + POSIX 信号量读取 facereco_video 推流，
并提供最新帧给 EyeEngine 进行叠加显示。

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

from __future__ import annotations

import ctypes
import ctypes.util
import logging
import struct
import time
from dataclasses import dataclass
from typing import Optional, Tuple

import numpy as np

from .config import LcdSide

logger = logging.getLogger(__name__)


@dataclass
class VideoFrameMeta:
    frame_id: int
    timestamp_ns: int
    width: int
    height: int
    pixel_format: int
    data_size: int
    flags: int


class VideoStreamConsumer:
    """读取 facereco_video 共享内存中的最新帧。"""

    # C++ 常量保持一致
    _FRAME_SIZE = 512 * 1024  # VideoFrame::kMaxFrameSize
    _RING_FRAMES = 3
    _RING_DATA_SIZE = _FRAME_SIZE * _RING_FRAMES

    # VideoRingBuffer 对齐字段偏移（与 C++ alignas(64) 一致）
    _WRITE_IDX_OFFSET = _RING_DATA_SIZE
    _READ_IDX_OFFSET = _WRITE_IDX_OFFSET + 64
    _TOTAL_WRITTEN_OFFSET = _READ_IDX_OFFSET + 64
    _TOTAL_DROPPED_OFFSET = _TOTAL_WRITTEN_OFFSET + 64
    _LAST_UPDATE_OFFSET = _TOTAL_DROPPED_OFFSET + 64
    _TOTAL_SIZE = ((_LAST_UPDATE_OFFSET + 8 + 63) // 64) * 64

    # VideoFrameHeader 解析格式（packed + 64 字节对齐，末尾 3 字节 padding）
    _HEADER_STRUCT = struct.Struct("<QQIIBIB31s")

    def __init__(
        self,
        resource_id: str = "facereco_video",
        instance_id: int = 0,
    ) -> None:
        self.resource_id = resource_id
        self.instance_id = int(instance_id)
        self._libc = None
        self._librt = None
        self._shm_ptr = None
        self._shm_addr = None
        self._shm_id = None
        self._sem_filled = None
        self._sem_empty = None
        self._initialized = False

    def initialize(self) -> bool:
        if self._initialized:
            return True

        self._libc = ctypes.CDLL("libc.so.6", use_errno=True)
        # sem_* 在 librt 或 libc 中
        librt_path = ctypes.util.find_library("rt")
        if librt_path:
            self._librt = ctypes.CDLL(librt_path, use_errno=True)
        else:
            self._librt = self._libc

        # ftok("/tmp", instance_id)
        self._libc.ftok.argtypes = [ctypes.c_char_p, ctypes.c_int]
        self._libc.ftok.restype = ctypes.c_int
        key = self._libc.ftok(b"/tmp", self.instance_id)
        if key == -1:
            err = ctypes.get_errno()
            logger.error("VideoStreamConsumer: ftok failed: errno=%s", err)
            return False

        # shmget
        self._libc.shmget.argtypes = [ctypes.c_int, ctypes.c_size_t, ctypes.c_int]
        self._libc.shmget.restype = ctypes.c_int
        shm_id = self._libc.shmget(key, self._TOTAL_SIZE, 0o600)
        if shm_id == -1:
            err = ctypes.get_errno()
            logger.error("VideoStreamConsumer: shmget failed: errno=%s", err)
            return False

        # shmat
        self._libc.shmat.argtypes = [ctypes.c_int, ctypes.c_void_p, ctypes.c_int]
        self._libc.shmat.restype = ctypes.c_void_p
        shm_addr = self._libc.shmat(shm_id, None, 0)
        if shm_addr in (ctypes.c_void_p(-1).value, None):
            err = ctypes.get_errno()
            logger.error("VideoStreamConsumer: shmat failed: errno=%s", err)
            return False

        # sem_open
        self._librt.sem_open.argtypes = [ctypes.c_char_p, ctypes.c_int]
        self._librt.sem_open.restype = ctypes.c_void_p

        filled_name = f"/{self.resource_id}_{self.instance_id}_filled".encode()
        empty_name = f"/{self.resource_id}_{self.instance_id}_empty".encode()
        sem_filled = self._librt.sem_open(filled_name, 0)
        sem_empty = self._librt.sem_open(empty_name, 0)

        if sem_filled in (ctypes.c_void_p(-1).value, None):
            err = ctypes.get_errno()
            logger.error("VideoStreamConsumer: sem_open filled failed: errno=%s", err)
            return False
        if sem_empty in (ctypes.c_void_p(-1).value, None):
            err = ctypes.get_errno()
            logger.error("VideoStreamConsumer: sem_open empty failed: errno=%s", err)
            return False

        # sem_trywait / sem_post
        self._librt.sem_trywait.argtypes = [ctypes.c_void_p]
        self._librt.sem_trywait.restype = ctypes.c_int
        self._librt.sem_post.argtypes = [ctypes.c_void_p]
        self._librt.sem_post.restype = ctypes.c_int

        self._shm_id = shm_id
        self._shm_addr = shm_addr
        self._shm_ptr = ctypes.cast(shm_addr, ctypes.POINTER(ctypes.c_uint8))
        self._sem_filled = sem_filled
        self._sem_empty = sem_empty
        self._initialized = True
        logger.info("VideoStreamConsumer: initialized (resource_id=%s, instance_id=%s)", self.resource_id, self.instance_id)
        return True

    def shutdown(self) -> None:
        if not self._initialized:
            return
        try:
            self._libc.shmdt.argtypes = [ctypes.c_void_p]
            self._libc.shmdt.restype = ctypes.c_int
            if self._shm_addr:
                self._libc.shmdt(self._shm_addr)
        except Exception:
            logger.exception("VideoStreamConsumer: shmdt failed")
        self._initialized = False

    def _sem_wait_with_timeout(self, timeout_ms: int) -> bool:
        """使用 sem_trywait + 轮询实现超时等待。"""
        deadline = time.time() + max(0, timeout_ms) / 1000.0
        while True:
            result = self._librt.sem_trywait(self._sem_filled)
            if result == 0:
                return True
            if time.time() >= deadline:
                return False
            time.sleep(0.001)

    def _sem_post_empty(self) -> None:
        if self._sem_empty:
            self._librt.sem_post(self._sem_empty)

    def _read_uint32(self, offset: int) -> int:
        return ctypes.c_uint32.from_address(self._shm_addr + offset).value

    def _write_uint32(self, offset: int, value: int) -> None:
        ctypes.c_uint32.from_address(self._shm_addr + offset).value = value

    def get_latest_frame(self, timeout_ms: int = 100) -> Tuple[Optional[np.ndarray], Optional[VideoFrameMeta]]:
        if not self._initialized:
            return None, None

        if not self._sem_wait_with_timeout(timeout_ms):
            return None, None

        try:
            latest_frame = None
            latest_meta = None

            while True:
                read_idx = self._read_uint32(self._READ_IDX_OFFSET)
                frame_offset = read_idx * self._FRAME_SIZE
                header_bytes = ctypes.string_at(self._shm_addr + frame_offset, 64)
                fields = self._HEADER_STRUCT.unpack(header_bytes[:self._HEADER_STRUCT.size])
                frame_id, timestamp_ns, width, height, pixel_format, data_size, flags, _ = fields

                if flags & 0x01 == 0 or data_size <= 0:
                    self._sem_post_empty()
                    latest_frame = None
                    latest_meta = None
                else:
                    pixel_offset = frame_offset + 64
                    pixel_bytes = ctypes.string_at(self._shm_addr + pixel_offset, data_size)

                    latest_meta = VideoFrameMeta(
                        frame_id=frame_id,
                        timestamp_ns=timestamp_ns,
                        width=width,
                        height=height,
                        pixel_format=pixel_format,
                        data_size=data_size,
                        flags=flags,
                    )
                    latest_frame = self._decode_frame(pixel_bytes, width, height, pixel_format)

                    # 释放 empty
                    self._sem_post_empty()

                # 推进读指针
                next_idx = (read_idx + 1) % self._RING_FRAMES
                self._write_uint32(self._READ_IDX_OFFSET, next_idx)

                # 尝试快速丢弃旧帧，保留最新帧
                if self._librt.sem_trywait(self._sem_filled) != 0:
                    break

            return latest_frame, latest_meta
        except Exception:
            logger.exception("VideoStreamConsumer: read frame failed")
            self._sem_post_empty()
            return None, None

    def _decode_frame(self, pixel_bytes: bytes, width: int, height: int, pixel_format: int) -> Optional[np.ndarray]:
        if width <= 0 or height <= 0:
            return None

        try:
            if pixel_format in (1, 3):  # RGB888 or BGR888
                frame = np.frombuffer(pixel_bytes, dtype=np.uint8).reshape((height, width, 3))
                if pixel_format == 3:
                    frame = frame[..., ::-1]
                return frame
            if pixel_format in (2, 4):  # RGBA/BGRA
                frame = np.frombuffer(pixel_bytes, dtype=np.uint8).reshape((height, width, 4))
                if pixel_format == 4:
                    frame = frame[..., [2, 1, 0, 3]]
                # 丢弃 alpha
                return frame[..., :3]
            if pixel_format == 5:  # grayscale
                gray = np.frombuffer(pixel_bytes, dtype=np.uint8).reshape((height, width))
                return np.stack([gray, gray, gray], axis=-1)
        except Exception:
            logger.exception("VideoStreamConsumer: decode failed")
            return None

        return None


def resize_frame(frame: np.ndarray, target_size: int = 240) -> np.ndarray:
    """缩放到 LCD 大小（240x240）。"""
    if frame is None:
        return frame
    h, w = frame.shape[:2]
    if h == target_size and w == target_size:
        return frame

    # 快速裁剪到正方形（避免昂贵的 resize）
    if h == target_size and w > target_size:
        start = (w - target_size) // 2
        return frame[:, start:start + target_size]
    if w == target_size and h > target_size:
        start = (h - target_size) // 2
        return frame[start:start + target_size, :]

    # 其他情况回退到 resize
    try:
        import cv2
        return cv2.resize(frame, (target_size, target_size), interpolation=cv2.INTER_AREA)
    except Exception:
        try:
            from PIL import Image
            img = Image.fromarray(frame)
            img = img.resize((target_size, target_size), Image.BILINEAR)
            return np.array(img)
        except Exception:
            logger.exception("VideoStreamConsumer: resize failed")
            return frame


def target_side_from_config(side_value) -> LcdSide:
    if isinstance(side_value, LcdSide):
        return side_value
    if isinstance(side_value, str):
        side = side_value.upper()
        if side == "LEFT":
            return LcdSide.LEFT
        if side == "RIGHT":
            return LcdSide.RIGHT
    if isinstance(side_value, int):
        return LcdSide.LEFT if side_value == 0 else LcdSide.RIGHT
    return LcdSide.RIGHT
