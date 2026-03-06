#!/usr/bin/env python3
"""
测试 audio_player_service 中的重复播放功能
- playback_mode: single|repeat_count|repeat_duration
- play_count: 重复次数
- play_duration_ms: 总播放时长（ms）
- repeat_interval_ms: 重复之间的间隔（ms）
"""

import zmq
import json
import time

def send_audio_command(sock, command_dict, timeout_ms=2000):
    """发送一条音频命令并获取回复"""
    cmd = {"action": "cmd.audio.play"}
    cmd.update(command_dict)
    
    print(f"\n📤 Sending: {json.dumps(cmd, indent=2)}")
    sock.send_string(json.dumps(cmd))
    
    try:
        # REQ/REP 模式：必须等待回复才能发送下一条消息
        # 设置接收超时以避免无限阻塞
        sock.setsockopt(zmq.RCVTIMEO, timeout_ms)
        reply = sock.recv_string()
        print(f"✅ Reply: {reply}")
        return reply
    except zmq.Again:
        print(f"⏳ Timeout waiting for reply ({timeout_ms}ms)")
        return None
    except zmq.ZMQError as e:
        print(f"❌ ZMQ Error: {e}")
        return None

def main():
    # 连接到 audio_player 服务
    ctx = zmq.Context()
    sock = ctx.socket(zmq.REQ)
    
    # 尝试连接
    try:
        sock.connect('ipc:///tmp/doly_audio_player_cmd.sock')
        print("✅ Connected to audio_player service")
    except Exception as e:
        print(f"❌ Failed to connect: {e}")
        print("\n💡 Please start audio_player_service first:")
        print("   cd ~/dolydev/libs/audio_player/build")
        print("   ./audio_player_service")
        return
    
    # 测试 1: 单次播放（默认）
    # print("\n" + "="*60)
    # print("Test 1: Single play (default, backward compatible)")
    # print("="*60)
    # send_audio_command(sock, {
    #     "alias": "sfx_police_1",
    #     "priority": 5,
    #     "volume": 0.8
    # })
    # time.sleep(3)
    
    # 测试 2: 重复播放（3次，间隔500ms）- 使用短音频
    print("\n" + "="*60)
    print("Test 2: Repeat count mode (3x with 500ms interval)")
    print("="*60)
    send_audio_command(sock, {
        "alias": "sfx_camera_click",
        "priority": 5,
        "volume": 0.8,
        "playback_mode": "repeat_count",
        "play_count": 3,
        "repeat_interval_ms": 500
    })
    print("\n⏳ Waiting 6 seconds for 3 plays (475ms each) + intervals (500ms each)...")
    print("   Expected: play1(475ms) → interval(500ms) → play2(475ms) → interval(500ms) → play3(475ms)")
    time.sleep(6)
    
    # 测试 3: 时长模式（3秒循环一个较短的音频）
    print("\n" + "="*60)
    print("Test 3: Repeat duration mode (3s total loop)")
    print("="*60)
    send_audio_command(sock, {
        "alias": "sfx_collect",
        "priority": 5,
        "volume": 0.8,
        "playback_mode": "repeat_duration",
        "play_duration_ms": 5000,
        "repeat_interval_ms": 100
    })
    print("\n⏳ Waiting 5 seconds for 3s loop...")
    time.sleep(5)
    
    # 测试 4: 停止所有音频
    print("\n" + "="*60)
    print("Test 4: Stop all sounds")
    print("="*60)
    print("(Stop functionality to be tested separately)")
    
    print("\n" + "="*60)
    print("✅ All tests completed!")
    print("="*60)
    print("\n💡 Check the audio_player_service logs for repeat debug messages:")
    print("   - '[AudioPlayer] repeat: alias=X count=Y/Z'")
    print("   - '[AudioPlayer] repeat_duration restart: ...'")
    print("   - '[AudioPlayer] repeat_duration complete: ...'")
    
    sock.close()
    ctx.term()

if __name__ == "__main__":
    main()
