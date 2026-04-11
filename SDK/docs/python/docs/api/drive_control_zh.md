# DriveControl API 参考

导入：

```python
import doly_drive
```

本页记录 `doly_drive` Python 模块公开的 API。

## 枚举

### `DriveErrorType`

取值：

- `Abort`
- `Force`
- `Rotate`
- `Motor`

### `DriveMotorSide`

取值：

- `Both`
- `Left`
- `Right`

### `DriveState`

取值：

- `Running`
- `Completed`
- `Error`

### `DriveType`

取值：

- `Freestyle`
- `XY`
- `Distance`
- `Rotate`

## 类

### `DriveEventListener`

监听器接口。实现此类并通过 `add_listener()` 注册以接收事件。

**方法**

- `on_drive_complete(id: int) -> None`
  - 驱动命令成功完成时调用。
- `on_drive_error(id: int, side: DriveMotorSide, type: DriveErrorType) -> None`
  - 驱动操作报告错误时调用。
- `on_drive_state_change(drive_type: DriveType, state: DriveState) -> None`
  - 驱动操作状态变化时调用。

## 函数

### `add_listener(listener_obj: DriveEventListener) -> None`

注册监听器对象以接收驱动事件。

**参数**

- **listener_obj**: 指向监听器实例的引用（不得为 null）。

**注意**

- 回调通常由内部工作/事件线程调用。


### `remove_listener(listener_obj: DriveEventListener) -> None`

取消注册监听器对象。

**参数**

- **listener_obj**: 之前传入 `add_listener()` 的引用。


### `init(imu_off_gx: int = 0, imu_off_gy: int = 0, imu_off_gz: int = 0, imu_off_ax: int = 0, imu_off_ay: int = 0, imu_off_az: int = 0) -> int`

初始化驱动控制模块。

IMU 偏移是平台保存的校准值。

**参数**

- **imu_off_gx**: IMU 陀螺 X 偏移（校准）。(默认：`0`)
- **imu_off_gy**: IMU 陀螺 Y 偏移（校准）。(默认：`0`)
- **imu_off_gz**: IMU 陀螺 Z 偏移（校准）。(默认：`0`)
- **imu_off_ax**: IMU 加速度计 X 偏移（校准）。(默认：`0`)
- **imu_off_ay**: IMU 加速度计 Y 偏移（校准）。(默认：`0`)
- **imu_off_az**: IMU 加速度计 Z 偏移（校准）。(默认：`0`)

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : already active / already initialized - -1 : motor setup failed - -2 : IMU init failed
```


### `dispose(dispose_IMU: bool) -> int`

关闭驱动模块并释放资源，可选择同时关闭 IMU。移除回调函数。

**参数**

- **dispose_IMU**: 若为 `True`，同时关闭 IMU 模块。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : not active / not initialized
```


### `is_active() -> bool`

检查模块是否已初始化并处于激活状态。

**返回**

- `bool`:

```text
true if active, false otherwise.
```


### `abort() -> None`

立即中止当前驱动操作（通常用于紧急停止）。


### `free_drive(speed: int, is_left: bool, to_forward: bool) -> bool`

手动驱动（低级接口）。

**参数**

- **speed**: 电机速度百分比 (0..100)。
- **is_left**: `True` = 左电机，`False` = 右电机。
- **to_forward**: `True` = 前进，`False` = 后退。

**返回**

- `bool`:

```text
true if accepted.
```

**注意**

- 非阻塞；由另一个线程处理。


### `go_xy(id: int, x: int, y: int, speed: int, to_forward: bool, with_brake: bool = False, acceleration_interval: int = 0, control_speed: bool = False, control_force: bool = True) -> bool`

自主移动到 (x, y) 目标（高级接口）。

**参数**

- **id**: 用户命令 id（在事件/回调中返回）。
- **x**: 目标 X（坐标系由应用定义）。
- **y**: 目标 Y（与 x 使用相同单位）。
- **speed**: 请求的速度百分比 (0..100)。
- **to_forward**: 移动方向偏好。
- **with_brake**: 若为 `True`，在结束时制动。(默认：`False`)
- **acceleration_interval**: 加速步长间隔（0 表示禁用）。(默认：`0`)
- **control_speed**: 动态启用速度控制。(默认：`False`)
- **control_force**: 启用牵引/力控制。(默认：`True`)

**返回**

- `bool`:

```text
true if command accepted.
```

**注意**

- 非阻塞；由另一个线程处理。


### `go_distance(...)`, `go_rotate(...)` 等函数的说明类似，参见英文 API。

### `get_position() -> Position`

获取当前估计位置。

**返回**

- `Position`:

```text
Current Position estimate (see Helper.h for definition/units).
```


### `reset_position() -> None`

重置当前位置估计为 (0, 0, 0)（字段由实现定义）。


### `get_state() -> DriveState`

获取当前驱动状态。


### `get_rpm(is_left: bool) -> float`

获取当前电机转速（RPM）。


### `on_complete(...)`, `on_error(...)`, `on_state_change(...)`, `clear_listeners()`

用于注册/取消注册静态回调，行为与英文 API 相同。
