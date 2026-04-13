import argparse
import json
import os
import sys
import time

import zmq


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="FaceReco 单模块命令测试工具")
    parser.add_argument("--endpoint", default="ipc:///tmp/doly_events.sock", help="ZMQ PUB 端点")
    parser.add_argument("--status-endpoint", default="ipc:///tmp/doly_vision_events.sock", help="FaceReco 事件总线端点，用于等待 ready")
    parser.add_argument("--bind", action="store_true", help="以 bind 方式启动 PUB，适合单独测试 LiveFaceReco")
    parser.add_argument("--wait-ready", action="store_true", help="发送命令前先等待 FaceReco 在事件总线上广播 ready/state")
    parser.add_argument("--ready-timeout", type=float, default=10.0, help="等待 ready/state 的最长秒数")
    parser.add_argument("--warmup", type=float, default=0.5, help="发送前等待秒数")
    parser.add_argument("--hold-after-send", type=float, default=0.5, help="最后一次发送后继续保持 socket 存活的秒数")
    parser.add_argument("--repeat", type=int, default=1, help="重复发送次数")
    parser.add_argument("--interval", type=float, default=0.5, help="重复发送间隔秒数")
    parser.add_argument("--pre-mode", choices=["IDLE", "STREAM_ONLY", "DETECT_ONLY", "DETECT_TRACK", "FULL"], help="在功能命令前先发送一次模式切换")
    parser.add_argument("--pre-timeout", type=int, default=30, help="预发送模式切换的 timeout")
    parser.add_argument("--pre-wait", type=float, default=2.0, help="预发送模式后等待秒数")

    subparsers = parser.add_subparsers(dest="command", required=True)

    mode_parser = subparsers.add_parser("mode", help="发送模式切换")
    mode_parser.add_argument("mode", choices=["IDLE", "STREAM_ONLY", "DETECT_ONLY", "DETECT_TRACK", "FULL"], help="目标模式")
    mode_parser.add_argument("--timeout", type=int, default=30)

    photo_parser = subparsers.add_parser("photo", help="发送拍照命令")
    photo_parser.add_argument("--request-id", default="photo_test")
    photo_parser.add_argument("--save-path", default="/home/pi/dolydev/libs/FaceReco/captures")
    photo_parser.add_argument("--filename", default="")
    photo_parser.add_argument("--format", default="jpg")
    photo_parser.add_argument("--quality", type=int, default=95)
    photo_parser.add_argument("--include-annotations", action="store_true")
    photo_parser.add_argument("--require-face", action="store_true")

    video_start_parser = subparsers.add_parser("video-start", help="开始录像")
    video_start_parser.add_argument("--request-id", default="video_test")
    video_start_parser.add_argument("--save-path", default="/home/pi/dolydev/libs/FaceReco/videos")
    video_start_parser.add_argument("--filename", default="")
    video_start_parser.add_argument("--fps", type=int, default=15)
    video_start_parser.add_argument("--max-duration", type=int, default=0)
    video_start_parser.add_argument("--include-annotations", action="store_true")

    video_stop_parser = subparsers.add_parser("video-stop", help="停止录像")
    video_stop_parser.add_argument("--request-id", default="video_test")

    return parser


def build_message(args: argparse.Namespace):
    if args.command == "mode":
        return "cmd.vision.mode", {
            "target": "facereco",
            "mode": args.mode,
            "timeout": args.timeout,
        }
    if args.command == "photo":
        payload = {
            "request_id": args.request_id,
            "save_path": args.save_path,
            "format": args.format,
            "quality": args.quality,
            "include_annotations": args.include_annotations,
            "save_snapshot": True,
        }
        if args.filename:
            payload["filename"] = args.filename
        if args.require_face:
            payload["require_face"] = True
        return "cmd.vision.capture.photo", payload
    if args.command == "video-start":
        payload = {
            "request_id": args.request_id,
            "save_path": args.save_path,
            "fps": args.fps,
            "max_duration_seconds": args.max_duration,
            "include_annotations": args.include_annotations,
        }
        if args.filename:
            payload["filename"] = args.filename
        return "cmd.vision.capture.video.start", payload
    if args.command == "video-stop":
        return "cmd.vision.capture.video.stop", {"request_id": args.request_id}
    raise ValueError(f"未知命令: {args.command}")


def ipc_path(endpoint: str) -> str:
    if endpoint.startswith("ipc://"):
        return endpoint[len("ipc://"):]
    return ""


def cleanup_ipc_endpoint(endpoint: str) -> None:
    path = ipc_path(endpoint)
    if not path:
        return
    if os.path.exists(path):
        os.unlink(path)
        print(f"removed stale ipc socket: {path}")


def recv_message(socket: zmq.Socket):
    parts = socket.recv_multipart()
    if not parts:
        return "", {}

    topic = parts[0].decode("utf-8", errors="replace")
    payload_text = "{}"
    if len(parts) > 1:
        payload_text = parts[1].decode("utf-8", errors="replace")
    elif b" " in parts[0]:
        first = parts[0].decode("utf-8", errors="replace")
        topic, payload_text = first.split(" ", 1)

    try:
        payload = json.loads(payload_text)
    except json.JSONDecodeError:
        payload = {"raw": payload_text}
    return topic, payload


def wait_for_ready(context: zmq.Context, endpoint: str, timeout: float) -> bool:
    subscriber = context.socket(zmq.SUB)
    subscriber.setsockopt(zmq.LINGER, 0)
    subscriber.setsockopt(zmq.RCVTIMEO, 200)
    subscriber.connect(endpoint)
    subscriber.setsockopt(zmq.SUBSCRIBE, b"status.vision.ready")
    subscriber.setsockopt(zmq.SUBSCRIBE, b"status.vision.state")

    deadline = time.monotonic() + max(timeout, 0.0)
    print(f"waiting for FaceReco ready on {endpoint} ...")
    try:
        while time.monotonic() < deadline:
            try:
                topic, payload = recv_message(subscriber)
            except zmq.Again:
                continue

            print(f"observed {topic}: {json.dumps(payload, ensure_ascii=False)}")
            if topic == "status.vision.ready":
                return True
            if topic == "status.vision.state":
                return True
    finally:
        subscriber.close()

    return False


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    topic, payload = build_message(args)
    context = zmq.Context()
    socket = context.socket(zmq.PUB)
    socket.setsockopt(zmq.LINGER, 0)

    if args.bind:
        cleanup_ipc_endpoint(args.endpoint)
        socket.bind(args.endpoint)
    else:
        socket.connect(args.endpoint)

    if args.wait_ready:
        if not wait_for_ready(context, args.status_endpoint, args.ready_timeout):
            print(f"timeout waiting for FaceReco ready/state from {args.status_endpoint}", file=sys.stderr)
            socket.close()
            context.term()
            return 2

    time.sleep(max(args.warmup, 0.0))

    if args.pre_mode:
        pre_message = {
            "target": "facereco",
            "mode": args.pre_mode,
            "timeout": args.pre_timeout,
        }
        socket.send_multipart([b"cmd.vision.mode", json.dumps(pre_message).encode("utf-8")])
        print(f"sent cmd.vision.mode: {json.dumps(pre_message, ensure_ascii=False)}")
        if args.pre_wait > 0:
            time.sleep(args.pre_wait)

    for index in range(args.repeat):
        message = dict(payload)
        if args.repeat > 1 and "request_id" in message:
            message["request_id"] = f"{payload['request_id']}_{index + 1}"
        socket.send_multipart([topic.encode("utf-8"), json.dumps(message).encode("utf-8")])
        print(f"sent {topic}: {json.dumps(message, ensure_ascii=False)}")
        if index + 1 < args.repeat:
            time.sleep(max(args.interval, 0.0))

    if args.hold_after_send > 0:
        time.sleep(args.hold_after_send)

    socket.close()
    context.term()
    return 0


if __name__ == "__main__":
    sys.exit(main())
