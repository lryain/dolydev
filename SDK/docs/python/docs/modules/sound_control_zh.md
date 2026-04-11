# SoundControl（Python）

## 导入

```python
import doly_helper
```

## 最小可用示例

下面是模块的 `example.py` 作为起点。

```python
"""
example.py

It demonstrates:
- Initializing the Sound controller
- Registering the static event listener
- Seting volume
- Playin sound
- Cleaning up (dispose)

"""

import time
import doly_helper as helper
import doly_sound as snd


def on_snd_begin(id: int, volume):
    print(f"[info] Sound begin id={id} side={volume:.3f}")

def on_snd_complete(id: int):
    print(f"[info] Sound completed id={id}")

def on_snd_abort(id: int):
    print(f"[info] Sound aborted id={id}")

def on_snd_error(id: int):
    print(f"[error] Sound error id={id}")


def main():

    # Version
    try:
        print(f"[info] LedControl Version: {snd.get_version():.3f}")
    except AttributeError:
        pass

    # *** IMPORTANT *** 
    # Stop doly service if running,
    # otherwise instance of libraries cause conflict	
    if helper.stop_doly_service() < 0: 
        print("[error] Doly service stop failed")
        return -1
        

    # Initialize sound controler
    rc = snd.init()
    if rc < 0:
        print(f"[error] SoundControl.init failed rc={rc}")
        return -2
    
    # Register static event listeners if required
    snd.on_begin(on_snd_begin)
    snd.on_complete(on_snd_complete)
    snd.on_abort(on_snd_error)
    snd.on_error(on_snd_error)

	# Set volume to 80%
    snd.set_volume(80)

    # Play sound
    rc = snd.play("sound_test.wav", 1) < 0
    if rc < 0:
        print(f"[error] Play failed rc={rc}")
        return -3

    # Wait until done (simple polling example)
    while snd.get_state() != snd.SoundState.Stop:
        time.sleep(0.1)

    # Cleanup
    snd.dispose()    
    time.sleep(0.2) 

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

## 常见任务

- **停止 Doly 服务**
- **初始化**
- **添加事件监听器**
- **播放**
- **读取状态 / 值**
- **释放 / 清理**

## 备注

- **平台：** Raspberry Pi OS
- **Python：** 3.11
- **机器人预装：** 是

## API 参考

见： [API reference](../api/sound_control.md)
