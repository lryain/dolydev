import zmq, json, time
ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.connect('ipc:///tmp/doly_audio_player_cmd.sock')
# 测试音频条目：使用字典形式指定 alias 或 uri
paths = [
    {"alias": "all"},
    # {"alias": "mp3_aw_faded"},
    # {"alias": "bless"},
    # {"uri": "/home/pi/dolydev/assets/music/nature.mp3"},
    # {"uri": "/home/pi/dolydev/assets/testing/voice/voice.wav"},
    # {"uri": "/home/pi/dolydev/assets/sounds/error.mp3"},
    # /home/pi/dolydev/assets/sounds/music/AlanWalker-Faded.mp3
    # {"uri": "/home/pi/dolydev/assets/sounds/music/AlanWalker-Faded.mp3"},
    # {"uri": "/home/pi/dolydev/assets/lose_funny_retro_video-game-80925.mp3"}
]
for p in paths:
    req = {"action": "cmd.audio.stop"}
    if isinstance(p, dict):
        req.update(p)
    else:
        # 兼容旧格式（字符串 uri）
        req["uri"] = p
    print('sending', p)
    sock.send_string(json.dumps(req))
    try:
        rep = sock.recv_string(flags=0)
        print('reply', rep)
    except Exception as e:
        print('recv error', e)
    time.sleep(0.4)
