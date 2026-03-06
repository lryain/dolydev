#!/usr/bin/env python3
"""
WebSocket ASR 客户端测试脚本
测试 Streaming Zipformer 模型的实时识别功能
"""

import asyncio
import json
import sys
import os
import wave
from pathlib import Path

try:
    import websockets
except ImportError:
    print("Error: websockets library not installed")
    print("Install with: pip install websockets")
    sys.exit(1)


async def test_asr(server_url="ws://localhost:8001/sttRealtime", audio_file=None):
    """连接到 ASR 服务器并发送音频进行识别"""
    
    if audio_file is None:
        # 使用默认的测试音频文件
        audio_file = "assets/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/test_wavs/0.wav"
    
    if not os.path.exists(audio_file):
        print(f"错误: 音频文件不存在: {audio_file}")
        return False
    
    try:
        # 打开音频文件
        with wave.open(audio_file, 'rb') as wav_file:
            frames = wav_file.readframes(wav_file.getnframes())
            sample_rate = wav_file.getframerate()
            num_channels = wav_file.getnchannels()
            sample_width = wav_file.getsampwidth()
        
        print(f"✓ 读取音频文件: {audio_file}")
        print(f"  - 采样率: {sample_rate} Hz")
        print(f"  - 声道数: {num_channels}")
        print(f"  - 样本宽度: {sample_width}")
        print(f"  - 总大小: {len(frames)} bytes\n")
        
        # 发起 WebSocket 连接
        print(f"连接到服务器: {server_url}")
        async with websockets.connect(server_url) as websocket:
            print("✓ WebSocket 连接成功\n")
            
            # 发送初始配置
            config = {
                "sample_rate": sample_rate,
                "num_channels": num_channels,
                "sample_width": sample_width,
            }
            await websocket.send(json.dumps(config))
            print(f"✓ 发送配置: {config}\n")
            
            # 分块发送音频数据 (每 40ms 一块)
            chunk_size = int(sample_rate * 40 / 1000) * num_channels * sample_width
            start_idx = 0
            
            print("发送音频数据...\n")
            while start_idx < len(frames):
                end_idx = min(start_idx + chunk_size, len(frames))
                chunk = frames[start_idx:end_idx]
                
                # 将音频块作为 base64 编码的字符串发送
                import base64
                await websocket.send(base64.b64encode(chunk).decode('utf-8'))
                
                start_idx = end_idx
                await asyncio.sleep(0.04)  # 模拟实时流
            
            print("✓ 音频传输完成")
            
            # 发送结束信号
            await websocket.send(json.dumps({"done": True}))
            print("✓ 已发送完成信号\n")
            
            # 接收识别结果
            print("等待识别结果...\n")
            results = []
            try:
                while True:
                    response = await asyncio.wait_for(websocket.recv(), timeout=5.0)
                    result = json.loads(response)
                    results.append(result)
                    
                    print(f"收到结果:")
                    print(f"  - 文本: {result.get('text', 'N/A')}")
                    print(f"  - 部分: {result.get('partial', False)}")
                    
                    # 检查 Zipformer 特定字段
                    if 'language' in result:
                        print(f"  - 语言: {result.get('language', 'N/A')}")
                    if 'emotion' in result:
                        print(f"  - 情绪: {result.get('emotion', 'N/A')}")
                    if 'event' in result:
                        print(f"  - 事件: {result.get('event', 'N/A')}")
                    
                    print()
                    
                    # 如果收到最终结果则退出
                    if not result.get('partial', False):
                        break
                        
            except asyncio.TimeoutError:
                print("✓ 识别完成 (超时)\n")
            
            if results:
                final_result = results[-1] if results else {}
                print(f"\n{'='*50}")
                print("最终识别结果:")
                print(f"{'='*50}")
                print(json.dumps(final_result, indent=2, ensure_ascii=False))
                print(f"{'='*50}\n")
                
                # 验证结果
                if final_result.get('text'):
                    print("✓ 测试成功! 获得了识别文本")
                    return True
                else:
                    print("! 警告: 没有获得识别文本")
                    return False
            else:
                print("! 警告: 没有收到任何结果")
                return False
                
    except ConnectionRefusedError:
        print(f"错误: 无法连接到服务器 {server_url}")
        print("请确保服务器正在运行")
        return False
    except Exception as e:
        print(f"错误: {e}")
        import traceback
        traceback.print_exc()
        return False


async def main():
    """主测试函数"""
    print("="*60)
    print("Streaming Zipformer 模型 WebSocket 测试")
    print("="*60)
    print()
    
    # 获取当前目录
    script_dir = Path(__file__).parent
    os.chdir(script_dir)
    
    # 测试多个音频文件
    test_files = [
        "assets/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/test_wavs/0.wav",
        "assets/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/test_wavs/1.wav",
    ]
    
    success_count = 0
    for audio_file in test_files:
        if os.path.exists(audio_file):
            print(f"\n{'—'*60}")
            print(f"测试文件: {audio_file}")
            print(f"{'—'*60}\n")
            
            result = await test_asr(audio_file=audio_file)
            if result:
                success_count += 1
            
            await asyncio.sleep(1)  # 测试间等待
        else:
            print(f"跳过: {audio_file} (不存在)")
    
    print(f"\n{'='*60}")
    print(f"测试完成: {success_count}/{len([f for f in test_files if os.path.exists(f)])} 成功")
    print(f"{'='*60}\n")


if __name__ == "__main__":
    asyncio.run(main())
