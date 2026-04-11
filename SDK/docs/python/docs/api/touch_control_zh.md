# TouchControl API 参考

导入：

```python
import doly_touch
```

本页记录 `doly_touch` Python 模块公开的 API。

## 枚举

### `TouchSide`

取值：

- `Both`
- `Left`
- `Right`

### `TouchState`

取值：

- `Up`
- `Down`

### `TouchActivity`

取值：

- `Patting`
- `Disturb`

## 类

### `TouchEventListener`

监听器接口。实现此类并通过 `add_listener()` 注册以接收事件。

**方法**

- `onTouchEvent(side: TouchSide, state: TouchState) -> None`
  - 触摸状态变化时调用。
- `onTouchActivityEvent(side: TouchSide, activity: TouchActivity) -> None`
  - 在检测到触摸活动（高阶模式）时调用。

## 函数

### `add_listener(listener: TouchEventListener, priority: bool = False) -> None`

注册监听器实例。

### `remove_listener(listener: TouchEventListener) -> None`

注销监听器。

### `on_touch(cb: py::function) -> None`, `on_touch_activity(cb: py::function) -> None`, `clear_listeners() -> None`

设置静态回调或清除所有回调（行为与英文文档一致）。

### `init() -> int`

初始化触摸控制器并启动工作线程。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : already initialized (no-op) - -1 : left sensor GPIO initialization failed - -2 : right sensor GPIO initialization failed
```


### `dispose() -> int`, `is_active() -> bool`, `is_touched(side: TouchSide) -> bool`, `get_version() -> float`

其余函数与英文 API 描述一致。