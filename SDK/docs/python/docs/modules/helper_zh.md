# Helper（Python）

## 导入

```python
import doly_helper
```

## 最小可用示例

下面是模块的 `example.py` 作为起点。

```python
import doly_helper as helper

def main():
    # Read settings
    res = helper.read_settings()
    if res < 0:
        print(f"[info] Read settings failed with code: {res}")
        return 1

    # Get IMU offsets
    res, gx, gy, gz, ax, ay, az = helper.get_imu_offsets()
    if res < 0:
        print(f"[info] Get IMU offsets failed with code: {res}")
        return 1

    print(f"[info] IMU Offsets - Gx:{gx} Gy:{gy} Gz:{gz} Ax:{ax} Ay:{ay} Az:{az}")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
```

## 常见任务

- **停止 Doly 服务**
- **读取设置**

## 备注

- **平台：** Raspberry Pi OS
- **Python：** 3.11
- **机器人预装：** 是

## API 参考

见： [API reference](../api/helper.md)
