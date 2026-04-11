# FanControl（Python）

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
- Initializing the Fan controller (manual mode)
- Seting Fan speed
- Cleaning up (dispose)

"""

import time
import doly_helper as helper
import doly_fan as fan

def main():

    # Version
    try:
        print(f"[info] FanControl Version: {fan.get_version():.3f}")
    except AttributeError:
        pass

    # *** IMPORTANT *** 
    # Stop doly service if running,
    # otherwise instance of libraries cause conflict	
    if helper.stop_doly_service() < 0: 
        print("[error] Doly service stop failed")
        return -1
    
    # Initialize Fan Control
    # auto_control = False: we will control fan speed manually in this example
    # auto_control = True: automatic fan control will try to manage CPU temperature under 70 degree Celsius
    rc = fan.init(False)
    if rc < 0:
        print(f"[error] FanControl.init failed rc={rc}")
        return -2
    
    # Set fan speed to 100%
    fan.set_fan_speed(100)
    print("[info] Fan speed set to 100%")
    
    # wait for X seconds to test it
    time.sleep(7)

    #Turn off fan
    fan.set_fan_speed(0)
    print("[info] Fan turned off")

    # Cleanup
    fan.dispose()  
    time.sleep(0.2) 

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

## 常见任务

- **停止 Doly 服务**
- **初始化**
- **设置风扇速度**
- **释放 / 清理**

## 备注

- **平台：** Raspberry Pi OS
- **Python：** 3.11
- **机器人预装：** 是

该模块使用 `/dev/doly_*` 设备。权限默认已配置；如遇问题，请检查用户权限（参见 **Troubleshooting → Permissions**）。

## API 参考

见： [API reference](../api/fan_control.md)
