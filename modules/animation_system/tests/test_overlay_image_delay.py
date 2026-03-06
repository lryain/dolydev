"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""

#!/usr/bin/env python3
"""
测试 EyeEngine ZMQ play_overlay_image/play_overlay_image_sync 及 delay_ms 参数
"""
import zmq
import json
import time

CMD_ENDPOINT = "ipc:///tmp/doly_eye_cmd.sock"

ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.setsockopt(zmq.RCVTIMEO, 3000)
sock.connect(CMD_ENDPOINT)

def send_cmd(action, **kwargs):
    req = {"action": action}
    req.update(kwargs)
    msg = json.dumps(req)
    sock.send_string(msg)
    try:
        reply = sock.recv_string()
    except Exception as e:
        return {"ok": False, "error": str(e)}
    try:
        return json.loads(reply)
    except Exception:
        return {"ok": False, "error": "invalid_json_reply", "raw": reply}

if __name__ == "__main__":
    print("[TEST] play_overlay_image (async, delay_ms=1500)...")
    t0 = time.time()
    resp = send_cmd("play_overlay_image", image="/home/pi/dolydev/assets/images/casino/apple.png", delay_ms=10, side="LEFT", scale=0.2)
    print("Response:", resp)
    print("实际延迟: %.2fs" % (time.time() - t0))
    time.sleep(0.1)

    print("[TEST] play_overlay_image_sync (sync, delay_ms=1500)...")
    t0 = time.time()
    resp = send_cmd("play_overlay_image", image="/home/pi/dolydev/assets/images/casino/banana.png", delay_ms=15, side="RIGHT", scale=0.2)
    print("Response:", resp)
    print("实际延迟: %.2fs" % (time.time() - t0))
