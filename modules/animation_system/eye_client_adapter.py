"""
Adapter that implements EyeInterface by calling libs.EyeEngineClient (pybind C++ engine)
This allows animation_system to call into the C++ engine without ZMQ.

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""
import asyncio
from typing import Optional
import logging
from animation_system.hardware_interfaces import EyeInterface

from pathlib import Path
import sys
repo_root = str(Path(__file__).resolve().parents[3])
sys.path.insert(0, str(Path(repo_root) / 'libs'))
from EyeEngineClient import EyeEngineClient

logger = logging.getLogger(__name__)

class EyeClientAdapter(EyeInterface):
    def __init__(self):
        self._client = EyeEngineClient()
        self._client.connect()

    async def play_animation(self, category: str, animation: str, priority: int = 5, hold_duration: float = 0.0) -> None:
        logger.info(f"[EyeClientAdapter] play_animation: {category}/{animation}")
        # call in background
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, lambda: self._client.send_command({'action':'play_animation','name':animation,'blocking':False,'hold':int(hold_duration)}))

    async def stop_animation(self) -> None:
        logger.info("[EyeClientAdapter] stop_animation")
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, lambda: self._client.send_command({'action':'stop'}))

    async def play_sequence_animations(self, sequence: str, side: str = 'BOTH', loop: bool = False, fps: Optional[int] = None, speed: float = 1.0) -> Optional[str]:
        logger.info(f"[EyeClientAdapter] play_sequence: {sequence}")
        loop_ = asyncio.get_running_loop()
        res = await loop_.run_in_executor(None, lambda: self._client.send_command({'action':'play_sequence','sequence':sequence,'side':side,'loop':loop,'fps':fps}))
        return res.get('overlay_id') if res and res.get('success') else None

    async def stop_overlay_sequence(self, overlay_id: str) -> bool:
        logger.info(f"[EyeClientAdapter] stop_overlay: {overlay_id}")
        loop = asyncio.get_running_loop()
        res = await loop.run_in_executor(None, lambda: self._client.send_command({'action':'stop_overlay','overlay_id':overlay_id}))
        return res and res.get('success', False)

    async def play_sprite_animation(self, category: str, animation: str, start: int = 0) -> Optional[str]:
        logger.info(f"[EyeClientAdapter] play_sprite: {category}/{animation}")
        loop = asyncio.get_running_loop()
        res = await loop.run_in_executor(None, lambda: self._client.send_command({'action':'play_sprite_animation','category':category,'animation':animation,'start':start}))
        return res.get('overlay_id') if res and res.get('success') else None

    async def play_behavior(self, behavior: str, level: int = 1, priority: int = 5, wait_completion: bool = False, hold_duration: float = 0.0) -> None:
        logger.info(f"[EyeClientAdapter] play_behavior: {behavior} level={level}")
        loop = asyncio.get_running_loop()
        await loop.run_in_executor(None, lambda: self._client.send_command({'action':'play_behavior','category':behavior,'level':level}))

    async def play_overlay_image(self, image: str, side: str = 'BOTH', loop: bool = False, fps: Optional[int] = None, x: int = 0, y: int = 0, scale: float = 1.0, rotation: float = 0.0, duration_ms: Optional[int] = None, delay_ms: Optional[int] = None) -> Optional[str]:
        logger.info(f"[EyeClientAdapter] play_overlay_image: {image}")
        loop_ = asyncio.get_running_loop()
        res = await loop_.run_in_executor(None, lambda: self._client.send_command({'action':'play_overlay_image','image':image,'side':side,'fps':fps,'loop':loop,'delay_ms':delay_ms,'duration_ms':duration_ms}))
        return res.get('overlay_id') if res and res.get('success') else None

    async def stop_overlay(self, overlay_id: str) -> bool:
        logger.info(f"[EyeClientAdapter] stop_overlay: {overlay_id}")
        loop = asyncio.get_running_loop()
        res = await loop.run_in_executor(None, lambda: self._client.send_command({'action':'stop_overlay','overlay_id':overlay_id}))
        return res and res.get('success', False)

    def close(self):
        try:
            self._client.close()
        except Exception:
            pass

