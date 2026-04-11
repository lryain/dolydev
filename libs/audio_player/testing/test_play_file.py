import zmq, json, time
ctx = zmq.Context()
sock = ctx.socket(zmq.REQ)
sock.connect('ipc:///tmp/doly_audio_player_cmd.sock')
# 测试音频条目：使用字典形式指定 alias 或 uri
paths = [
    # 指定uri文件路径 进行播放
    # {"alias": "animal_cow"},
    # {"uri": "/.doly/sounds/music/AlanWalker-Faded.mp3"},
    # {"uri": "/home/pi/Musics/ghsy-光辉岁月.flac"},
    {"alias": "music_alanwalker_faded"},
    {"alias": "music_beyond_guanghui_suiyue"},
    # {"alias": "mp3_aw_faded","priority":10, "ducking": True},
    # {"alias": "mp3_fss_huangmeixi","priority":10},

    # 播放背景音乐的同时，使用优先级和 ducking 参数播放一个短音效，测试背景音乐是否正确 ducking
    # {"alias": "mp3_aw_faded","priority":10},
    # {"alias": "animal_wolf", "ducking": True, "preempt": True, "priority": 2},
    # {"alias": "animal_dog", "priority": 9},
    # {"alias": "animal_chicken", "priority": 9},
    # {"alias": "animal_duck", "priority": 1},
    # # 当前max_concurrent_sounds 为3
    # {"alias": "animal_tiger", "priority": 9},
    # {"alias": "animal_pig", "priority": 9},
    
    # 指定播放时长和重复间隔，测试重复播放功能
    # {"alias": "sfx_collect",
    #     "priority": 6,
    #     "volume": 0.8,
    #     "playback_mode": "repeat_duration",
    #     "play_duration_ms": 3000,
    #     "repeat_interval_ms": 100
    # },
    
    # # 指定播放次数和重复间隔，测试重复播放功能
    # {
    #     "action": "cmd.audio.play",
    #     "alias": "sfx_debuff_2",
    #     "priority": 5,
    #     "preempt": False,
    #     "playback_mode": "repeat_count",
    #     "play_count": 3,
    #     "repeat_interval_ms": 100
    # }
        
    # {"alias": "animal_chicken", "priority":1,},
    # {"alias": "animal_crow", "priority":2, "preemptive": True},
    # {"alias": "animal_pig", "priority":3, "preemptive": False},
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

    # {"alias": "sfx_police_1"},
    # {"alias": "sfx_siren_2"},
    # {"alias": "sfx_slot_lever"},
    # {"alias": "sfx_slot_looser"},
    
    # {"alias": "laser"},



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
