import zmq, json, time
ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.connect('ipc:///tmp/doly_audio_player_cmd.sock')
# 测试音频条目：使用字典形式指定 alias 或 uri
paths = [
    # {"alias": "animal_cow"},
    # {"alias": "animal_cat"},
    # {"alias": "animal_chicken"},
    # {"alias": "animal_crow"},
    # {"alias": "animal_dog"},
    # {"alias": "animal_donkey"},
    # {"alias": "animal_duck"},
    # {"alias": "animal_goose"},
    # {"alias": "animal_horse"},
    # {"alias": "animal_pig"},
    # {"alias": "animal_wolf"},
    # {"alias": "animal_tiger"},
    # {"alias": "animal_zebra"},

    # {"alias": "sfx_alarm_1"},
    # {"alias": "sfx_alarm_2"},
    # {"alias": "sfx_alarm_3"},
    # {"alias": "sfx_alarm_4"},
    # {"alias": "sfx_alarm_5"},

    # {"alias": "sfx_buff_1"},
    # {"alias": "sfx_buff_2"},
    # {"alias": "sfx_buff_3"},
    # {"alias": "sfx_buff_4"},
    # {"alias": "sfx_buff_5"},
    # {"alias": "sfx_buff_6"},
    # {"alias": "sfx_camera_click"},
    # {"alias": "sfx_clock_push"},
    # {"alias": "sfx_clock_tick"},
    # {"alias": "sfx_coins"},
    # {"alias": "sfx_collect"},

    # {"alias": "sfx_damage"},

    # {"alias": "sfx_explosion_1"},
    # {"alias": "sfx_explosion_2"},
    # {"alias": "sfx_explosion_3"},

    # {"alias": "sfx_hmmm"},
    # {"alias": "sfx_level_up"},

    # {"alias": "sfx_lost_1"},
    # {"alias": "sfx_lost_2"},
    # {"alias": "sfx_lost_3"},

    # {"alias": "sfx_notification_1"},
    # {"alias": "sfx_notification_2"},
    # {"alias": "sfx_notification_3"},

    {"alias": "sfx_police_1"},
    {"alias": "sfx_siren_2"},
    {"alias": "sfx_slot_lever"},
    {"alias": "sfx_slot_looser"},
    
    # {"alias": "laser"},

    # {"alias": "mp3_aw_faded","priority":10, "ducking": True},
    # {"alias": "mp3_fss_huangmeixi","priority":10},

    # {"alias": "error"},
    # {"alias": "bless"},
    # {"uri": "/home/pi/dolydev/assets/music/nature.mp3"},
    # {"uri": "/home/pi/dolydev/assets/testing/voice/voice.wav"},
    # {"uri": "/home/pi/dolydev/assets/sounds/error.mp3"},
    # /home/pi/dolydev/assets/sounds/music/AlanWalker-Faded.mp3
    # /home/pi/dolydev/assets/sounds/music/huangmeixi.mp3
    # {"uri": "/home/pi/dolydev/assets/sounds/music/huangmeixi.mp3"},
    # {"uri": "/home/pi/dolydev/assets/lose_funny_retro_video-game-80925.mp3"}
]
for p in paths:
    req = {"action": "cmd.audio.play"}
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
