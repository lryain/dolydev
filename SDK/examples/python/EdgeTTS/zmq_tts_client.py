#!/usr/bin/env python3
"""
ZeroMQ TTS 客户端封装，配合 `zmq_tts_service.py` 使用。
提供简单 CLI，便于调试。
"""
import argparse
import json
import sys
import uuid
from typing import Any, Dict, List, Optional, Sequence

import zmq

DEFAULT_ENDPOINT = "ipc:///tmp/doly_tts_req.sock"
DEFAULT_AUDIO_PLAYER_ENDPOINT = "ipc:///tmp/doly_audio_player_cmd.sock"


class ZmqTTSClient:
    """ZeroMQ REQ/REP 客户端。"""

    def __init__(self, endpoint: str = DEFAULT_ENDPOINT, timeout_ms: int = 10000):
        self.endpoint = endpoint
        self.timeout_ms = timeout_ms
        self.ctx = zmq.Context.instance()
        self.sock = self.ctx.socket(zmq.REQ)
        self.sock.connect(self.endpoint)
        self.sock.setsockopt(zmq.RCVTIMEO, self.timeout_ms)

    def synthesize(
        self,
        text: str,
        *,
        voice: str = "zh-CN-YunxiaNeural",
        pitch: str = "+0Hz",
        volume: str = "+0%",
        rate: str = "+0%",
        fmt: str = "wav",
        play: bool = False,
        play_mode: str = "audio_player",
        audio_player: Optional[Dict[str, Any]] = None,
        req_id: Optional[str] = None,
    ) -> Dict[str, Any]:
        if not text.strip():
            raise ValueError("text 不能为空")
        payload: Dict[str, Any] = {
            "action": "tts.synthesize",
            "req_id": req_id or str(uuid.uuid4()),
            "text": text,
            "voice": voice,
            "pitch": pitch,
            "volume": volume,
            "rate": rate,
            "format": fmt,
            "play": play,
            "play_mode": play_mode,
        }
        if audio_player:
            payload["audio_player"] = audio_player
        # REQ/REP 必须严格一发一收
        self.sock.send_string(json.dumps(payload))
        reply = self.sock.recv_string()
        return json.loads(reply)

    def close(self) -> None:
        try:
            self.sock.close(0)
        except Exception:
            pass


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="ZeroMQ TTS 客户端", allow_abbrev=False)
    parser.add_argument("--endpoint", default=DEFAULT_ENDPOINT, help="ZeroMQ 服务器地址")
    parser.add_argument("--text", required=True, help="要合成的文本")
    parser.add_argument("--voice", default="zh-CN-YunxiaNeural", help="发音人")
    parser.add_argument("--pitch", default="+0Hz", help="音调，如 +50Hz/-20Hz")
    parser.add_argument("--rate", default="+0%", help="语速，如 -20%/+10%")
    parser.add_argument("--volume", default="+0%", help="音量，如 -20%/+10%")
    parser.add_argument("--format", default="wav", choices=["mp3", "wav"], help="输出格式")
    parser.add_argument("--play", action="store_true", help="服务端合成后播放")
    parser.add_argument("--play-mode", default="audio_player", choices=["local", "audio_player"], help="播放方式")
    parser.add_argument("--ap-endpoint", default=DEFAULT_AUDIO_PLAYER_ENDPOINT, help="audio_player_service 地址")
    parser.add_argument("--ap-priority", type=int, default=50, help="audio_player 优先级")
    parser.add_argument("--ap-volume", type=float, default=0.9, help="audio_player 音量")
    parser.add_argument("--timeout", type=int, default=10000, help="接收超时 ms")
    parser.add_argument("--debug", action="store_true", help="打印请求/响应")
    return parser


def _normalize_cli_args(args: Sequence[str]) -> List[str]:
    normalized: List[str] = []
    skip_next = False
    for idx, token in enumerate(args):
        if skip_next:
            skip_next = False
            continue
        if token in ("--rate", "--pitch", "--volume") and idx + 1 < len(args):
            value = args[idx + 1]
            if not value.startswith("--"):
                normalized.append(f"{token}={value}")
                skip_next = True
                continue
        normalized.append(token)
    return normalized


def main() -> None:
    parser = build_parser()
    args = parser.parse_args(_normalize_cli_args(sys.argv[1:]))

    client = ZmqTTSClient(endpoint=args.endpoint, timeout_ms=args.timeout)
    audio_player_cfg = None
    if args.play_mode == "audio_player":
        audio_player_cfg = {
            "endpoint": args.ap_endpoint,
            "priority": args.ap_priority,
            "volume": args.ap_volume,
        }

        if args.debug:
            print("[zmq-tts-client] 请求:")
            print(json.dumps({
                "endpoint": args.endpoint,
                "text": args.text,
                "voice": args.voice,
                "pitch": args.pitch,
                "rate": args.rate,
                "volume": args.volume,
                "format": args.format,
                "play": args.play,
                "play_mode": args.play_mode,
                "audio_player": audio_player_cfg,
            }, ensure_ascii=False, indent=2))

    try:
        resp = client.synthesize(
            text=args.text,
            voice=args.voice,
            pitch=args.pitch,
            volume=args.volume,
            rate=args.rate,
            fmt=args.format,
            play=args.play,
            play_mode=args.play_mode,
            audio_player=audio_player_cfg,
        )
        if args.debug:
            print("[zmq-tts-client] 响应:")
        print(json.dumps(resp, ensure_ascii=False, indent=2))
    except Exception as e:  # noqa: BLE001
        print(f"调用失败: {e}")
        sys.exit(1)
    finally:
        client.close()


if __name__ == "__main__":
    main()
