# ImuControl API 参考

导入：

```python
import doly_imu
```

本页记录 `doly_imu` Python 模块公开的 API。

## 枚举

### `ImuGesture`

取值：

- `Undefined`
- `Move`
- `LongShake`
- `ShortShake`
- `Vibrate`
- `VibrateExtreme`
- `ShockLight`
- `ShockMedium`
- `ShockHard`
- `ShockExtreme`

### `GestureDirection`

取值：

- `Left`
- `Right`
- `Up`
- `Down`
- `Front`
- `Back`

## 类

### `VectorFloat`

**字段**

- **x**
- **y**
- **z**

### `YawPitchRoll`

**字段**

- **yaw**
- **pitch**
- **roll**

### `ImuData`

**字段**

- **ypr**
- **linear_accel**
- **temperature**

### `ImuEventListener`

监听器接口。实现此类并通过 `add_listener()` 注册以接收事件。

**方法**

- `onImuUpdate(data: ImuData) -> None`
  - 当可用新的 IMU 样本/更新时调用。
- `onImuGesture(type: ImuGesture, from: GestureDirection) -> None`
  - 当检测到手势时调用。

## 函数

### `add_listener(listener: ImuEventListener, priority: bool = False) -> None`

注册 `ImuEventListener` 实例。

### `remove_listener(listener: ImuEventListener) -> None`

注销之前注册的监听器。

### `on_update(cb: py::function) -> None`

设置静态 IMU 更新回调（替换之前的回调）。

### `on_gesture(cb: py::function) -> None`

设置静态手势回调（替换之前的回调）。

### `clear_listeners() -> None`

注销所有静态回调并清除保存的 Python 函数引用。

### `init(delay: int = 0, gx: int = 0, gy: int = 0, gz: int = 0, ax: int = 0, ay: int = 0, az: int = 0) -> int`

初始化 IMU 子系统。必须先调用以获得有效数据。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : already active - -1 : init failed
```

**注意**

- 采样率：104Hz（加速计+陀螺），1Hz（温度）。


### 其它函数

`dispose()`, `calculate_offsets(...)`, `get_imu_data()`, `get_temperature()`, `get_version()` 等函数语义与英文 API 相同。