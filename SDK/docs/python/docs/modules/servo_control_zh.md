# ServoControl（Python）

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
- Initializing the external servo controller (IO plug servos)
- Setting static event callbacks
- Setting servo position
- Releasing servo hold
- Cleaning up (dispose)

"""

import time
import doly_helper as helper
import doly_servo as servo

def on_servo_abort(id, channel):
	print(f"[info] Servo abort id:{id} channel:{channel}")
     
def on_servo_complete(id, channel):
	print(f"[info] Servo complete id:{id} channel:{channel}")

def on_servo_error(id, channel):
	print(f"[info] Servo error id:{id} channel:{channel}")


def main():

    # Version
    try:
        print(f"[info] ServoControl Version: {servo.get_version():.3f}")
    except AttributeError:
        pass

    # *** IMPORTANT *** 
    # Stop doly service if running,
    # otherwise instance of libraries cause conflict	
    if helper.stop_doly_service() < 0: 
        print("[error] Doly service stop failed")
        return -1
    
    # Initialize servo controler
    rc = servo.init()
    if rc < 0:
        print(f"[error] ServoControl.init failed rc={rc}")
        return -2
    
    # Add static event listeners
    servo.on_complete(on_servo_complete)
    servo.on_abort(on_servo_abort)
    servo.on_error(on_servo_error)
    
    # Initialize sound controler to test sound file
    servo.set_servo(1, servo.ServoId.Servo0, 180, 100, False)
    servo.set_servo(2, servo.ServoId.Servo1, 90, 1, False)

    # wait for to complete
    time.sleep(3)

    # Optional release function; disables servo hold position
    servo.release(servo.ServoId.Servo0)
    servo.release(servo.ServoId.Servo1)

    # Cleanup
    servo.dispose() 
    time.sleep(0.2) 

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
```

## 常见任务

- **停止 Doly 服务**
- **初始化**
- **添加事件监听器**
- **设置伺服角度**
- **读取状态 / 值**
- **释放 / 清理**

## 备注

- **平台：** Raspberry Pi OS
- **Python：** 3.11
- **机器人预装：** 是

## API 参考

见： [API reference](../api/servo_control.md)
