"""
## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com"""


import asyncio
import logging
import sys
import os
from pathlib import Path

# 添加项目跟路径
sys.path.append("/home/pi/dolydev")

from modules.animation_system.interfaces.audio_player_interface import AudioPlayerInterface

async def test_audio():
    logging.basicConfig(level=logging.DEBUG)
    player = AudioPlayerInterface(debug=True)
    try:
        # 尝试播放一个存在的 alias
        await player.play("animal", "cat")
    except Exception as e:
        print(f"Test failed: {e}")

if __name__ == "__main__":
    asyncio.run(test_audio())
