# ServoControl API 参考

导入：

```python
import doly_servo
```

本页记录 `doly_servo` Python 模块公开的 API。

## 枚举

### `ServoId`

取值：

- `Servo0`
- `Servo1`

## 类

### `ServoEventListener`

监听器接口。实现此类并通过 `add_listener()` 注册以接收事件。

**方法**

- `onServoAbort(id: int, channel: ServoId) -> None`
  - 伺服动作被中止时调用。
- `onServoError(id: int, channel: ServoId) -> None`
  - 伺服动作失败时调用。
- `onServoComplete(id: int, channel: ServoId) -> None`
  - 伺服动作完成时调用。

## 函数

### `add_listener(listener: ServoEventListener, priority: bool = False) -> None`

注册类式监听器（绑定会保存引用直到被移除）。

### `remove_listener(listener: ServoEventListener) -> None`

取消注册监听器。

### `on_complete(cb: py::function) -> None`, `on_abort(cb: py::function) -> None`, `on_error(cb: py::function) -> None`

设置静态回调（会替换之前设置的同类型回调）。

### `clear_listeners() -> None`

注销所有静态回调并清除保存的 Python 函数引用。

### `init() -> int`

初始化伺服子系统。

**返回**

- `int`:

```text
Status code: - 0 : success - -1 : SERVO_0 setup failed - -2 : SERVO_1 setup failed
```


### `set_servo(id: int, channel: ServoId, angle: float, speed: int = 100, invert: bool = False) -> int`

设置伺服目标角度。

**参数**

- **id**: 用户定义的动作标识（在事件回调中转发）。
- **channel**: 伺服通道。
- **angle**: 目标角度（度）。
- **speed**: 速度百分比（0..100）。(默认：`100`)
- **invert**: 若为 `True`，反转该通道方向。(默认：`False`)

**返回**

- `int`:

```text
Status code: - 0 : success - -1 : max angle exceed error - -2 : speed range error (0..100) - -3 : undefined channel - -4 : not initialized
```


### `abort(channel: ServoId) -> int`, `release(channel: ServoId) -> int`, `dispose() -> int`, `get_version() -> float`

其余函数语义与英文 API 相同：中止、释放保持力、释放资源并获取版本号。