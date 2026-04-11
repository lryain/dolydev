# Helper API 参考

导入：

```python
import doly_helper
```

本页记录 `doly_helper` Python 模块公开的 API。

## 类

### `Position`

**字段**

- **head**
- **x**
- **y**

### `Position2F`

**字段**

- **x**
- **y**

## 函数

### `read_settings() -> int`

从平台设置文件读取默认设置（常用于加载校准/配置值）。

**返回**

- `int`:

```text
Status code: - >= 0 : success - -1 : settings file not found - -2 : XML open or parse error
```


### `get_imu_offsets(gx: int, gy: int, gz: int, ax: int, ay: int, az: int) -> int`

检索 IMU 校准偏移值（陀螺/加速度计）。

**参数**

- **gx, gy, gz**: 陀螺轴偏移。
- **ax, ay, az**: 加速度计轴偏移。

**返回**

- `int`:

```text
Status code: - 0 : success - -1 : failed to read offsets
```


### `stop_doly_service() -> int`

停止后台 Doly 服务（如在运行）。

某些应用/测试可能需要独占硬件资源，此帮助程序可请求停止服务以释放资源。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : service not active - -1 : error while stopping service
```
