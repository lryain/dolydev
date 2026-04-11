import zmq, json, time
ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.connect('ipc:///tmp/doly_audio_player_cmd.sock')
# 测试音频条目：使用字典形式指定 alias 或 uri
paths = [
    # 停止所有音频播放
    {"alias": "all"}, # {"uri": "all"},
    # 按路径停止
    # {"uri": "/home/pi/Musics/ghsy-光辉岁月.flac"},
    # {"uri": "/.doly/sounds/music/AlanWalker-Faded.mp3"},
    # 按别名停止
    # {"alias": "music_alanwalker_faded"},
    # {"alias": "music_beyond_guanghui_suiyue"},
    # {"alias": "bless"},
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
