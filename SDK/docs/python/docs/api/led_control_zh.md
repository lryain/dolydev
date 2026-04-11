# LedControl API 参考

导入：

```python
import doly_led
```

本页记录 `doly_led` Python 模块公开的 API。

## 枚举

### `LedSide`

取值：

- `Both`
- `Left`
- `Right`

### `LedActivityState`

取值：

- `Free`
- `Running`
- `Completed`

### `LedErrorType`

取值：

- `Abort`

## 类

### `LedActivity`

**字段**

- **mainColor**
- **fadeColor**
- **fade_time**
- **state**

### `LedEventListener`

监听器接口。实现此类并通过 `add_listener()` 注册以接收事件。

**方法**

- `onLedComplete(id: int, side: LedSide) -> None`
  - 活动完成时调用。
- `onLedError(id: int, side: LedSide, type: LedErrorType) -> None`
  - 活动失败或被中止时调用。

## 函数

### `add_listener(listener: LedEventListener, priority: bool = False) -> None`

注册 `LedEventListener` 实例。

### `remove_listener(listener: LedEventListener) -> None`

注销监听器。

### `on_complete(cb: py::function) -> None`, `on_error(cb: py::function) -> None`, `clear_listeners() -> None`

用于设置静态回调或清除所有回调（行为与英文 API 相同）。

### `init() -> int`

初始化 LED 子系统。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : already initialized - -1 : R Left GPIO init failed - -2 : G Left GPIO init failed - -3 : B Left GPIO init failed - -4 : R Right GPIO init failed - -5 : G Right GPIO init failed - -6 : B Right GPIO init failed
```


### `dispose() -> int`, `is_active() -> bool`, `abort(side: LedSide) -> None`, `process_activity(id: int, side: LedSide, activity: LedActivity) -> None`, `get_version() -> float`

其余函数与英文文档描述一致：处理异步活动、查询状态、释放资源并获取版本号。