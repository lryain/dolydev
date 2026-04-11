# TouchControl（Python）

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
- Initializing the touch controller
- Registering the static event listeners
- Cleaning up (remove_listener + dispose)

"""


import time
import doly_helper as helper
import doly_touch as touch

def on_touch(side, state):
    print(f"[info] Touch Event side={side} state={state}")

def on_touch_activity(side, activity):
    print(f"[info] Touch Activity side={side} type={activity}")

def main():

    # Version
    try:
        print(f"[info] TouchControl Version: {touch.get_version():.3f}")
    except AttributeError:
        pass

    # *** IMPORTANT *** 
    # Stop doly service if running,
    # otherwise instance of libraries cause conflict	
    if helper.stop_doly_service() < 0: 
        print("[error] Doly service stop failed")
        return -1
        

    # Initialize TouchControl
    rc = touch.init()
    if rc < 0:
        print(f"[error] TouchControl.init failed rc={rc}")
        return -2  

    # Register static event listeners 
    touch.on_touch(on_touch)
    touch.on_touch_activity(on_touch_activity)    

    # Run for 30 seconds to test touch events
    print(f"[info] Run for 30 seconds to test touch events")
    time.sleep(30) 

    # Cleanup     
    touch.dispose()       
    time.sleep(0.2) 

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

## 常见任务

- **停止 Doly 服务**
- **初始化**
- **添加事件监听器**
- **读取状态 / 值**
- **释放 / 清理**

## 备注

- **平台：** Raspberry Pi OS
- **Python：** 3.11
- **机器人预装：** 是

如果此模块使用 `/dev/doly_*` 设备，请确保用户权限已正确配置（参见 **Troubleshooting → Permissions**）。

## API 参考

见： [API reference](../api/touch_control.md)
