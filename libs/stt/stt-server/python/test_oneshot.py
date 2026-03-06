import json
import wave
import sys
import time
import socket
import base64
import struct

def test_oneshot(file_path):
    import asyncio
    import websockets

    async def run():
        uri = "ws://localhost:8001/oneshot"
        async with websockets.connect(uri) as ws:
            # 获取音频数据
            with wave.open(file_path, 'rb') as wf:
                params = wf.getparams()
                audio_data = wf.readframes(params.nframes)
            
            print(f"发送控制消息: start")
            await ws.send(json.dumps({"type": "control", "cmd": "start"}))
            
            print(f"发送音频数据: {len(audio_data)} 字节")
            await ws.send(audio_data)
            
            print(f"发送控制消息: stop")
            await ws.send(json.dumps({"type": "control", "cmd": "stop"}))
            
            # 等待结果
            result = await ws.recv()
            print(f"收到结果: {result}")

    asyncio.run(run())

if __name__ == "__main__":
    test_oneshot("0.wav")
