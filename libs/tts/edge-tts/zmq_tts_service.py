#!/usr/bin/env python3
"""
ZeroMQ TTS 服务（edge-tts 驱动）。
- REQ/REP：默认 ipc:///tmp/doly_tts_req.sock
- 文本合成、保存 mp3/wav，可选播放（local 或 audio_player）
- 适配 Doly 现有 ZeroMQ 架构，便于后续替换 HTTP 版 tts-server。
"""
import argparse
import asyncio
import json
import logging
import os
import shutil
import subprocess
import uuid
from pathlib import Path
from typing import Dict, Optional

import edge_tts
import zmq
import zmq.asyncio

DEFAULT_BIND = "ipc:///tmp/doly_tts_req.sock"
DEFAULT_AUDIO_PLAYER_ENDPOINT = "ipc:///tmp/doly_audio_player_cmd.sock"
DEFAULT_OUTPUT_DIR = Path(__file__).parent / "files"
DEFAULT_VOICE = "zh-CN-YunxiaNeural"
DEFAULT_FORMAT = "wav"
DEFAULT_SAMPLE_RATE = 22050


class TTSServer:
    """ZeroMQ TTS 服务核心。"""

    def __init__(
        self,
        bind: str = DEFAULT_BIND,
        output_dir: Path = DEFAULT_OUTPUT_DIR,
        default_voice: str = DEFAULT_VOICE,
        audio_player_endpoint: str = DEFAULT_AUDIO_PLAYER_ENDPOINT,
    ) -> None:
        self.bind = bind
        self.output_dir = Path(output_dir)
        self.output_dir.mkdir(parents=True, exist_ok=True)
        self.default_voice = default_voice
        self.audio_player_endpoint = audio_player_endpoint
        # 使用全局 async ZMQ 上下文，创建 REP 套接字
        self.ctx = zmq.asyncio.Context.instance()
        self.sock = self.ctx.socket(zmq.REP)
        self.sock.bind(self.bind)
        logging.info("TTS ZeroMQ 服务已启动，监听 %s，输出目录=%s", self.bind, self.output_dir)

    async def serve_forever(self) -> None:
        """主循环：接收请求并处理。"""
        while True:
            try:
                msg = await self.sock.recv()
            except asyncio.CancelledError:
                break
            try:
                req = json.loads(msg.decode("utf-8"))  # 解码/解析 JSON
            except Exception:
                await self.sock.send_json({"ok": False, "error": {"code": "bad_json", "message": "无效 JSON"}})
                continue

            resp = await self._handle_request(req)
            try:
                await self.sock.send_json(resp)
            except Exception as e:  # noqa: BLE001
                logging.error("发送响应失败: %s", e)

    async def _handle_request(self, req: Dict) -> Dict:
        req_id = req.get("req_id") or str(uuid.uuid4())
        action = req.get("action")
        if action != "tts.synthesize":
            return {"ok": False, "req_id": req_id, "error": {"code": "bad_request", "message": "action 需为 tts.synthesize"}}

        text = (req.get("text") or "").strip()
        if not text:
            return {"ok": False, "req_id": req_id, "error": {"code": "bad_request", "message": "text 不能为空"}}

        # 基础参数，空则使用默认值
        voice = req.get("voice") or self.default_voice
        pitch = req.get("pitch") or "+0Hz"
        rate = req.get("rate") or "+0%"
        volume = req.get("volume") or "+0%"
        fmt = (req.get("format") or DEFAULT_FORMAT).lower()
        if fmt not in ("mp3", "wav"):
            return {"ok": False, "req_id": req_id, "error": {"code": "bad_request", "message": "format 仅支持 mp3|wav"}}

        play = bool(req.get("play", False))
        play_mode = req.get("play_mode") or "audio_player"
        if play_mode not in ("local", "audio_player"):
            return {"ok": False, "req_id": req_id, "error": {"code": "bad_request", "message": "play_mode 仅支持 local|audio_player"}}

        # 保存路径处理
        save_cfg = req.get("save") or {}
        save_dir = Path(save_cfg.get("dir") or self.output_dir)
        save_dir.mkdir(parents=True, exist_ok=True)
        filename = save_cfg.get("filename") or f"{uuid.uuid4()}.{fmt}"
        target_path = save_dir / filename

        timeout = float(req.get("timeout", 20.0))

        try:
            result = await asyncio.wait_for(
                self._synthesize_and_maybe_play(
                    text=text,
                    voice=voice,
                    pitch=pitch,
                    volume=volume,
                    rate=rate,
                    fmt=fmt,
                    path=target_path,
                    play=play,
                    play_mode=play_mode,
                    audio_player_cfg=req.get("audio_player") or {},
                ),
                timeout=timeout,
            )
        except asyncio.TimeoutError:
            return {"ok": False, "req_id": req_id, "error": {"code": "timeout", "message": f"合成超时 {timeout}s"}}
        except Exception as e:  # noqa: BLE001
            logging.exception("合成失败")
            return {"ok": False, "req_id": req_id, "error": {"code": "synth_error", "message": str(e)}}

        result.update({
            "ok": True,
            "req_id": req_id,
            "voice": voice,
            "pitch": pitch,
            "rate": rate,
            "volume": volume,
        })
        return result

    async def _synthesize_and_maybe_play(
        self,
        *,
        text: str,
        voice: str,
        pitch: str,
        volume: str,
        rate: str,
        fmt: str,
        path: Path,
        play: bool,
        play_mode: str,
        audio_player_cfg: Dict,
    ) -> Dict:
        """调用 edge-tts 合成并按需播放。"""
        tmp_mp3 = path.with_suffix(".mp3") if fmt == "wav" else path
        use_ssml = pitch != "+0Hz" or rate != "+0%"
        logging.info("收到请求 | voice=%s pitch=%s rate=%s volume=%s format=%s play=%s mode=%s text_len=%d",
                     voice, pitch, rate, volume, fmt, play, play_mode, len(text))

        # payload_text = text if not use_ssml else self._build_ssml(text, pitch, rate)
        communicate = edge_tts.Communicate(
            text,
            voice,
            rate=rate,
            volume=volume,
            pitch=pitch
        )
        await communicate.save(str(tmp_mp3))
        logging.info("edge-tts 合成完成 -> %s", tmp_mp3)

        if fmt == "wav":
            self._convert_to_wav(tmp_mp3, path)
            if tmp_mp3 != path and tmp_mp3.exists():
                try:
                    tmp_mp3.unlink()
                except Exception:
                    pass
        else:
            path = tmp_mp3

        final_path = path.resolve()

        playback_result = None
        if play:
            if play_mode == "local":
                playback_result = self._play_local(final_path)
            else:
                playback_result = await self._play_via_audio_player(final_path, audio_player_cfg)

        return {
            "path": str(final_path),
            "format": fmt,
            "sample_rate": DEFAULT_SAMPLE_RATE,
            # "ssml": use_ssml,
            "playback": playback_result,
        }

    @staticmethod
    def _build_ssml(text: str, pitch: str, rate: str) -> str:
        return f"<speak><prosody pitch=\"{pitch}\" rate=\"{rate}\">{text}</prosody></speak>"

    @staticmethod
    def _convert_to_wav(src_mp3: Path, dst_wav: Path) -> None:
        dst_wav.parent.mkdir(parents=True, exist_ok=True)
        try:
            subprocess.run([
                "ffmpeg", "-y", "-i", str(src_mp3),
                "-ar", str(DEFAULT_SAMPLE_RATE), "-ac", "1", "-f", "wav", str(dst_wav)
            ], check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        except FileNotFoundError:
            raise RuntimeError("ffmpeg 未安装，无法生成 wav")
        except subprocess.CalledProcessError as e:  # noqa: BLE001
            raise RuntimeError(f"ffmpeg 转换失败: {e}")

    @staticmethod
    def _play_local(path: Path) -> Dict:
        """在本机直接播放（不可控，简单兜底）。"""
        candidates = []
        if str(path).lower().endswith(".wav"):
            candidates.append(["aplay", str(path)])
        candidates.extend([
            ["mpg123", str(path)],
            ["mpv", "--no-video", str(path)],
            ["ffplay", "-nodisp", "-autoexit", str(path)],
        ])
        for cmd in candidates:
            if not shutil.which(cmd[0]):
                continue
            try:
                # 找到第一个可用播放器即返回
                subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
                return {"mode": "local", "sent": True, "player": cmd[0], "ok": True}
            except Exception as e:  # noqa: BLE001
                logging.warning("本地播放器 %s 失败: %s", cmd[0], e)
                continue
        return {"mode": "local", "sent": False, "error": "未找到可用播放器"}

    async def _play_via_audio_player(self, path: Path, cfg: Dict) -> Dict:
        endpoint = cfg.get("endpoint") or self.audio_player_endpoint
        payload = {
            "action": "cmd.audio.play",
            "uri": f"file://{path}",
            "priority": cfg.get("priority", 50),
            "volume": cfg.get("volume", 0.9),
            "alias": cfg.get("alias", "tts"),
            "ducking": cfg.get("ducking", True),
        }
        logging.info("转发到 audio_player_service | endpoint=%s payload=%s", endpoint, payload)
        ctx = zmq.asyncio.Context.instance()
        sock = ctx.socket(zmq.REQ)
        sock.setsockopt(zmq.RCVTIMEO, int(cfg.get("timeout_ms", 3000)))
        try:
            sock.connect(endpoint)
            await sock.send_string(json.dumps(payload))
            reply = await sock.recv_string()
            return {"mode": "audio_player", "sent": True, "reply": json.loads(reply)}
        except zmq.Again:
            return {"mode": "audio_player", "sent": False, "error": "audio_player 超时"}
        except Exception as e:  # noqa: BLE001
            logging.warning("audio_player 调用失败: %s", e)
            return {"mode": "audio_player", "sent": False, "error": str(e)}
        finally:
            sock.close(0)


async def _run(args: argparse.Namespace) -> None:
    server = TTSServer(
        bind=args.bind,
        output_dir=Path(args.output_dir),
        default_voice=args.default_voice,
        audio_player_endpoint=args.audio_player_endpoint,
    )
    await server.serve_forever()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="ZeroMQ edge-tts 服务", allow_abbrev=False)
    parser.add_argument("--bind", default=DEFAULT_BIND, help="ZeroMQ 监听地址，默认 ipc:///tmp/doly_tts_req.sock")
    parser.add_argument("--audio-player-endpoint", default=DEFAULT_AUDIO_PLAYER_ENDPOINT, help="audio_player_service REQ 地址")
    parser.add_argument("--output-dir", default=str(DEFAULT_OUTPUT_DIR), help="音频输出目录")
    parser.add_argument("--default-voice", default=DEFAULT_VOICE, help="默认发音人")
    parser.add_argument("--log-level", default="INFO", help="日志级别")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    logging.basicConfig(level=getattr(logging, args.log_level.upper(), logging.INFO),
                        format="%(asctime)s %(levelname)s [zmq-tts] %(message)s")
    loop = asyncio.get_event_loop()
    try:
        loop.run_until_complete(_run(args))
    except KeyboardInterrupt:
        logging.info("收到退出信号，停止服务")
    finally:
        tasks = asyncio.all_tasks(loop=loop)
        for t in tasks:
            t.cancel()
        loop.run_until_complete(asyncio.gather(*tasks, return_exceptions=True))
        loop.close()


if __name__ == "__main__":
    main()
