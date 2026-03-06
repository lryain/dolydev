"""
AudioPlayer 音频接口实现

通过 AudioPlayer 库播放声音，支持：
- 播放不同类型的声音（DOLY, MUSIC, SFX 等）
- 等待播放完成或非阻塞播放
- 停止当前播放
- 检查播放状态

日志格式：[AudioPlayerInterface] 操作描述

注意：AudioPlayer 使用 ZMQ 与音频播放器后台进程通信

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com
"""

import asyncio
import logging
import sys
import os
import zmq
import json
import time
from typing import Optional
from pathlib import Path
import re
try:
    import yaml
except Exception:
    yaml = None

from ..hardware_interfaces import SoundInterface

logger = logging.getLogger(__name__)


class AudioPlayerInterface(SoundInterface):
    """通过 ZMQ IPC 播放声音的接口实现"""
    
    def __init__(
        self,
        cmd_endpoint: str = "ipc:///tmp/doly_audio_player_cmd.sock",
        status_endpoint: str = "ipc:///tmp/doly_audio_player_status.sock",
        timeout_s: float = 30.0,
        debug: bool = False
    ):
        """
        初始化 AudioPlayer 接口
        
        Args:
            cmd_endpoint: 指令 ZMQ 地址
            status_endpoint: 状态 ZMQ 地址
            timeout_s: 播放超时时间（秒）
            debug: 是否启用调试日志
        """
        self.timeout_s = timeout_s
        self.debug = debug
        self.cmd_endpoint = cmd_endpoint
        self.status_endpoint = status_endpoint
        
        self.ctx = zmq.Context.instance()
        self.sock = self.ctx.socket(zmq.REQ)
        # 设置超时，避免 REQ 阻塞
        self.sock.setsockopt(zmq.RCVTIMEO, 1000)
        self.sock.setsockopt(zmq.SNDTIMEO, 1000)
        self.sock.setsockopt(zmq.LINGER, 0)
        
        self._is_playing_internal = False
        self._active_aliases = set()
        # alias map loaded from config/audio_player.yaml (alias -> path)
        self._alias_map = None
        
        try:
            self.sock.connect(self.cmd_endpoint)
            logger.info(f"[AudioPlayerInterface] 指令通道已连接到 {cmd_endpoint}")
            
            # 初始化状态订阅者
            self.sub = self.ctx.socket(zmq.SUB)
            self.sub.connect(self.status_endpoint)
            self.sub.setsockopt_string(zmq.SUBSCRIBE, "status.audio.playback")
            self.sub.setsockopt(zmq.RCVTIMEO, 10) # 非阻塞
            logger.info(f"[AudioPlayerInterface] 状态通道已连接到 {status_endpoint}")
            
        except Exception as e:
            logger.error(f"[AudioPlayerInterface] 连接失败: {e}")
    
    def _update_status(self):
        """尝试从 SUB 频道获取最新状态更新"""
        try:
            while True:
                topic = self.sub.recv_string(flags=zmq.NOBLOCK)
                data = self.sub.recv_string(flags=zmq.NOBLOCK)
                state = json.loads(data)
                
                # 更新活跃的 alias 列表
                self._active_aliases = {s.get("alias") for s in state.get("sounds", [])}
                self._is_playing_internal = state.get("active_sounds", 0) > 0
                
                if self.debug and self._is_playing_internal:
                    logger.debug(f"[AudioPlayerInterface] 活跃声音: {self._active_aliases}")
                    
        except zmq.Again:
            pass
        except Exception as e:
            logger.debug(f"[AudioPlayerInterface] 状态更新出错: {e}")

    async def play(
        self,
        type_id: Optional[str],
        name: str,
        wait: bool = True
    ) -> None:
        """
        播放声音
        
        Args:
            type_id: 声音类型（如 DOLY, MUSIC, SFX）或者 None
            name: 声音名称或 alias
            wait: 是否等待播放完成
        """
        try:
            # 构造初始候选 alias（保持与历史实现兼容）
            if type_id and not name.startswith(f"{type_id.lower()}_"):
                candidate = f"{type_id.lower()}_{name}"
            else:
                candidate = name

            # 尝试用 config 中的 alias_paths 做一次映射（容错、多种匹配）
            mapped = self._map_alias(type_id, candidate)
            alias = mapped if mapped else candidate.lower()

            logger.info(f"[AudioPlayerInterface] 播放音频 alias: {alias}, wait={wait}")

            req = {
                "action": "cmd.audio.play",
                "alias": alias
            }
            
            res = self._send_cmd(req)
            # 修改检查字段为 success，与 ZMQ 服务匹配
            if not res.get("success", False) and not res.get("ok", False):
                logger.error(f"[AudioPlayerInterface] 播放失败: {res.get('error', 'Unknown error')}")
                raise RuntimeError(f"AudioPlayer play failed: {res}")
            
            # 手动标记一下，防止状态更新还没到
            self._is_playing_internal = True
            self._active_aliases.add(alias)
            
            if wait:
                # 给一点时间让状态更新生效
                await asyncio.sleep(0.2)
                await self._wait_for_completion(alias)
                
        except Exception as e:
            logger.error(f"[AudioPlayerInterface] 播放音频异常: {e}")
            self._is_playing_internal = False
            raise

    async def _wait_for_completion(self, alias: str) -> None:
        """等待指定的 alias 播放完成"""
        start_time = time.time()
        logger.debug(f"[AudioPlayerInterface] 等待 {alias} 播放完成...")
        
        # 即使 alias 不在列表中了，可能因为还没开始或已经结束
        while time.time() - start_time < self.timeout_s:
            self._update_status()
            if alias not in self._active_aliases:
                # 检查是否因为还没更新到状态列表，如果已经等了 0.5s 还没出现，可能已经播完了（很短的声音）
                if time.time() - start_time > 0.5:
                    logger.debug(f"[AudioPlayerInterface] {alias} 不在活跃列表，检测完成")
                    return
            
            await asyncio.sleep(0.1)
        
        logger.warning(f"[AudioPlayerInterface] 播放 {alias} 超时")

    def _load_alias_map(self) -> None:
        """从项目配置中加载 alias_paths，缓存为字典（小写 key -> path）。"""
        if self._alias_map is not None:
            return

        cfg_path = None
        p = Path(__file__).resolve()
        # 向上查找 config/audio_player.yaml（最多向上遍历 6 层）
        for parent in p.parents:
            candidate = parent / "config" / "audio_player.yaml"
            if candidate.exists():
                cfg_path = candidate
                break

        if not cfg_path:
            logger.debug("[AudioPlayerInterface] 未找到 config/audio_player.yaml，跳过 alias 映射")
            self._alias_map = {}
            return

        if yaml is None:
            logger.warning("[AudioPlayerInterface] pyyaml 未安装，无法解析 audio_player.yaml，跳过 alias 映射")
            self._alias_map = {}
            return

        try:
            with open(cfg_path, 'r', encoding='utf-8') as fh:
                data = yaml.safe_load(fh) or {}
            aliases = data.get('alias_paths', {})
            # 规范化 keys 为小写，保留原 path
            self._alias_map = {k.lower(): v for k, v in aliases.items()}
            logger.debug(f"[AudioPlayerInterface] 加载 {len(self._alias_map)} 个 alias")
        except Exception as e:
            logger.warning(f"[AudioPlayerInterface] 解析 audio_player.yaml 失败: {e}")
            self._alias_map = {}

    def _map_alias(self, type_id: Optional[str], name: str) -> Optional[str]:
        """尝试将传入的 name 映射为配置中的 alias key（返回小写 alias 或 None）。

        匹配策略（优先级高->低）:
        1. 直接匹配 alias key
        2. 匹配 alias key 的简化形式（去掉 type 前缀）
        3. 如果传入的是文件名或路径，尝试匹配 alias_paths 中的 value（文件名或路径的 stem）
        4. 模糊包含匹配（value 包含 name）
        """
        self._load_alias_map()
        if not self._alias_map:
            return None

        # 规范化输入：小写，去掉形如 ' (3)' 的尾部计数，去掉额外空格
        name_l = name.lower().strip()
        # 移除末尾的 ' (number)'，比如 'doly_happy (3)'
        name_l = re.sub(r"\s*\(\d+\)\s*$", "", name_l)
        # 将空格替换为下划线以匹配 config 中常见的命名
        name_l = name_l.replace(' ', '_')

        # 1. 直接作为 alias key
        if name_l in self._alias_map:
            return name_l

        # Special case: patterns like 'HAPPY (1)' or 'happy (1)' -> try sound_happy_1
        # derive raw tag without possible type prefix
        raw = name
        if type_id and name.lower().startswith(f"{type_id.lower()}_"):
            # remove type prefix
            try:
                raw = name.split('_', 1)[1]
            except Exception:
                raw = name

        m = re.match(r"^\s*([A-Za-z0-9 _-]+?)\s*\(\s*(\d+)\s*\)\s*$", raw, re.IGNORECASE)
        if m:
            tag = m.group(1).strip().lower().replace(' ', '_')
            num = m.group(2)
            candidate_key = f"sound_{tag}_{num}"
            if candidate_key in self._alias_map:
                return candidate_key

        # 2. 去掉可能的 type 前缀（如 doly_）再匹配
        if '_' in name_l:
            _, rest = name_l.split('_', 1)
            if rest in self._alias_map:
                return rest

        # 2b. 如果 name 包含括号或其他噪音，尝试更简洁的 token 匹配
        simple = re.sub(r"[^a-z0-9_]+", "", name_l)
        if simple in self._alias_map:
            return simple

        # 3. 对于像 'some/path/file.mp3' 或 'file.mp3' 的 name，匹配 value 的 stem
        base = Path(name_l).stem
        for key, val in self._alias_map.items():
            try:
                if Path(val).stem.lower() == base:
                    return key
            except Exception:
                # val 可能不是路径，仅做包含匹配
                pass

        # 4. 包含匹配（如果 value 中包含 name）
        for key, val in self._alias_map.items():
            if name_l in val.lower() or val.lower() in name_l:
                return key

        return None

    async def stop(self) -> None:
        """停止所有播放"""
        req = {"action": "cmd.audio.stop", "alias": "all"}
        self._send_cmd(req)
        self._is_playing_internal = False
        self._active_aliases.clear()
        logger.info("[AudioPlayerInterface] 已停止所有音频")

    def is_playing(self) -> bool:
        """检查是否有声音在播放"""
        self._update_status()
        return self._is_playing_internal

    def close(self) -> None:
        self.sock.close()
        self.sub.close()

    def _send_cmd(self, req: dict) -> dict:
        """发送同步 ZMQ 请求并返回响应"""
        try:
            self.sock.send_string(json.dumps(req))
            rep_str = self.sock.recv_string()
            if self.debug:
                logger.debug(f"[AudioPlayerInterface] 收到响应: {rep_str}")
            return json.loads(rep_str)
        except Exception as e:
            logger.error(f"[AudioPlayerInterface] ZMQ 通信失败: {e}")
            # 重新实例化 socket 以重置 REQ 状态
            self.sock.close()
            self.sock = self.ctx.socket(zmq.REQ)
            self.sock.setsockopt(zmq.RCVTIMEO, 1000)
            self.sock.setsockopt(zmq.SNDTIMEO, 1000)
            self.sock.setsockopt(zmq.LINGER, 0)
            self.sock.connect(self.cmd_endpoint)
            return {"ok": False, "error": str(e)}
