# EdgeControl API 参考

导入：

```python
import doly_edge
```

本页记录 `doly_edge` Python 模块公开的 API。

## 枚举

### `GpioState`

取值：

- `Low`
- `High`

### `GapDirection`

取值：

- `Front`
- `Front_Left`
- `Front_Right`
- `Back`
- `Back_Left`
- `Back_Right`
- `Left`
- `Right`
- `Cross_Left`
- `Cross_Right`
- `All`

### `SensorId`

取值：

- `Back_Left`
- `Back_Right`
- `Front_Left`
- `Front_Right`

## 类

### `IrSensor`

**字段**

- **id**
- **state**

### `EdgeEventListener`

监听器接口。实现此类并通过 `add_listener()` 注册以接收事件。

**方法**

- `onEdgeChange(sensors: list[IrSensor]) -> None`
  - IR 传感器状态集合变化时调用。
- `onGapDetect(gap_type: GapDirection) -> None`
  - 当检测到缺口（例如缺失表面/坠落）并分类时调用。

## 函数

### `add_listener(listener: EdgeEventListener, priority: bool = False) -> None`

注册 `EdgeEventListener` 实例。

**参数**

- **listener**: 监听器对象。
- **priority**: 若为 `True`，监听器可能以更高优先级插入（实现定义）。(默认：`False`)


### `remove_listener(listener: EdgeEventListener) -> None`

取消注册监听器。


### `clear_listeners() -> None`

注销所有静态回调并清除绑定跟踪的监听器。


### `on_change(cb: py::function) -> None`

设置静态 on-change 回调（替换之前的回调）。提示：如需多个处理器请用分发函数。


### `on_gap_detect(cb: py::function) -> None`

设置静态 gap-detect 回调（替换之前的回调）。


### `init() -> int`

初始化边缘传感控制。

必须在启用控制或读取传感器前调用。

**返回**

- `int`:

```text
Status code: - 0 : success - <0 : error (implementation-defined)
```


### `dispose() -> int`

释放边缘传感控制资源并停止，移除回调函数。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : not initialized - -1..-6 : GPIO release failed (specific code indicates which GPIO failed)
```


### `is_active() -> bool`, `enable_control() -> int`, `disable_control() -> int`, `get_sensors(state: GpioState) -> list[IrSensor]`, `get_version() -> float`

其余函数用法与英文 API 相同，请参考原文以获取详细返回值与错误代码说明。