# ArmControl API 参考

导入：

```python
import doly_arm
```

本页记录 `doly_arm` Python 模块公开的 API。

## 枚举

### `ArmErrorType`

取值：

- `Abort`
- `Motor`

### `ArmSide`

取值：

- `Both`
- `Left`
- `Right`

### `ArmState`

取值：

- `Running`
- `Completed`
- `Error`

## 类

### `ArmData`

**字段**

- **side**
- **angle**

### `ArmEventListener`

监听器接口。实现此类并通过 `add_listener()` 注册以接收事件。

**方法**

- `onArmComplete(id: int, side: ArmSide) -> None`
  - 在机械臂命令成功完成时调用。
- `onArmError(id: int, side: ArmSide, error_type: ArmErrorType) -> None`
  - 在机械臂命令发生错误时调用。
- `onArmStateChange(side: ArmSide, state: ArmState) -> None`
  - 在机械臂状态变化时调用。
- `onArmMovement(side: ArmSide, degree_change: float) -> None`
  - 报告增量运动事件时调用。

## 函数

### `add_listener(listener: ArmEventListener, priority: bool = False) -> None`

注册监听器对象以接收机械臂事件。

**参数**

- **listener**: 指向监听器实例的引用（不得为 null）。
- **priority**: 如果为 `True`，以优先顺序插入监听器（实现可定义）。(默认：`False`)

**注意**

- 回调通常由内部工作线程/事件线程调用。


### `remove_listener(listener: ArmEventListener) -> None`

取消注册监听器对象。

**参数**

- **listener**: 之前传入 `add_listener()` 的监听器引用。


### `on_complete(cb: py::function) -> None`

设置静态完成回调（替换之前的回调）。
提示：如需多个处理器，请使用 Python 分发函数。

**参数**

- **cb**:


### `on_error(cb: py::function) -> None`

设置静态错误回调（替换之前的回调）。提示同上。

**参数**

- **cb**:


### `on_state_change(cb: py::function) -> None`

设置静态状态变化回调（替换之前的回调）。提示同上。

**参数**

- **cb**:


### `on_movement(cb: py::function) -> None`

设置静态运动回调（替换之前的回调）。提示同上。

**参数**

- **cb**:


### `clear_listeners() -> None`

注销所有静态回调并清除保存的 Python 函数引用。


### `init() -> int`

初始化机械臂子系统。

该函数必须在其它控制函数前调用。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : already initialized - -1 : left servo enable pin set failed - -2 : right servo enable pin set failed
```

**注意**

- 成功执行 `init()` 后，`isActive()` 应返回 true。


### `dispose() -> int`

停止并释放机械臂子系统资源，移除回调函数。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : not initialized
```


### `is_active() -> bool`

检查子系统是否已初始化并处于激活状态。

**返回**

- `bool`:

```text
true if active, false otherwise.
```


### `abort(side: ArmSide) -> None`

终止指定侧的当前操作（紧急停止用途）。

**参数**

- **side**: 要中止的机械臂侧（LEFT/RIGHT/BOTH）。


### `get_max_angle() -> int`

获取机械臂允许的最大角度（度）。

**返回**

- `int`:

```text
Maximum angle (degrees).
```


### `set_angle(id: int, side: ArmSide, speed: int, angle: int, with_brake: bool = False) -> int`

命令机械臂移动到指定角度。

此操作为异步（非阻塞），结果通过以下回调报告：
- `ArmEventListener::onArmComplete()`
- `ArmEventListener::onArmError()`
- `ArmEventListener::onArmStateChange()`

**参数**

- **id**: 用户定义的命令标识（将在完成/错误回调中返回）。
- **side**: 要移动的臂侧（LEFT/RIGHT/BOTH）。
- **speed**: 速度百分比，范围 [1..100]。
- **angle**: 目标角度，单位为度，范围 [0..getMaxAngle()]。
- **with_brake**: 若为 `True`，到达目标后执行制动/保持行为（实现定义）。(默认：`False`)

**返回**

- `int`:

```text
Status code: - 0 : success (command accepted) - -1 : not active (init() not called or subsystem not running) - -2 : speed out of range - -3 : angle out of range
```


### `get_state(side: ArmSide) -> ArmState`

获取指定侧当前操作状态。

**参数**

- **side**: 臂侧。

**返回**

- `ArmState`:

```text
Current ArmState.
```


### `get_current_angle(side: ArmSide) -> list[ArmData]`

获取指定侧当前角度值。

**参数**

- **side**: 请求的侧（LEFT/RIGHT/BOTH）。

**返回**

- `list[ArmData]`:

```text
Vector of ArmData entries. If side == BOTH, the vector may contain 2 elements.
```


### `get_version() -> float`

获取 SDK/库 版本号。

原始说明：格式 0.XYZ（主版本后 3 位数字）。

**返回**

- `float`:

```text
Version as float.
```
