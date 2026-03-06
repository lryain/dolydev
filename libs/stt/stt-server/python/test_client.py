import asyncio
import websockets
import wave
import json
import base64
import os

async def test_asr():
    # 更新端口为 8001
    uri = "ws://localhost:8001/sttRealtime"
    # uri = "ws://localhost:8001/oneshot"
    
    # 查找测试音频文件
    audio_file = "../assets/sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/test_wavs/3.wav"
    if not os.path.exists(audio_file):
        print(f"错误: 找不到音频文件 {audio_file}")
        return

    async with websockets.connect(uri) as websocket:
        print(f"已连接到 {uri}")
        
        # 打开音频文件获取参数
        with wave.open(audio_file, "rb") as wav_file:
            sample_rate = wav_file.getframerate()
            num_channels = wav_file.getnchannels()
            sample_width = wav_file.getsampwidth()
            
            # 发送配置信息
            config = {
                "sample_rate": sample_rate,
                "num_channels": num_channels,
                "sample_width": sample_width
            }
            await websocket.send(json.dumps(config))
            print(f"已发送配置: {config}")

            # 分块发送音频数据
            chunk_size = 4096 # 增加块大小
            data = wav_file.readframes(chunk_size)
            print("正在发送音频...")
            while data:
                await websocket.send(data)
                data = wav_file.readframes(chunk_size)
                # 稍微等待，但保持流的连贯性
                await asyncio.sleep(0.005) 
        
        # 发送结束标志
        await websocket.send(json.dumps({"done": True}))
        print("音频发送完毕，等待最终结果...")

        # 接收结果
        # 注意：这里我们持续监听，直到收到 finished 标志
        async for message in websocket:
            result = json.loads(message)
            print(f"收到结果: {result.get('text', '')}")
            if result.get('finished', False):
                print(f"--- 最终识别完整文本 ---: {result.get('text', '')}")
                break

# 运行测试
if __name__ == "__main__":
    asyncio.run(test_asr())