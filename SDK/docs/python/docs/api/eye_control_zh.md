# EyeControl API 参考

导入：

```python
import doly_eye
```

本页记录 `doly_eye` Python 模块公开的 API。

## 枚举

### `EyeSide`

取值：

- `Both`
- `Left`
- `Right`

### `IrisShape`

取值：

- `Classic`
- `Modern`
- `Space`
- `Orbit`
- `Glow`
- `Digi`

## 类

### `EyeEventListener`

监听器接口。实现此类并通过 `add_listener()` 注册以接收事件。

**方法**

- `onEyeStart(id: int) -> None`
  - 眼睛动作/动画开始时调用。
- `onEyeComplete(id: int) -> None`
  - 动画完成时调用。
- `onEyeAbort(id: int) -> None`
  - 动画被中止/停止时调用。

## 函数

### `add_listener(listener: EyeEventListener, priority: bool = False) -> None`

注册监听器以接收事件。

**参数**

- **listener**: 监听器实例。
- **priority**: 若为 `True`，监听器将放到分发列表前端。(默认：`False`)


### `remove_listener(listener: EyeEventListener) -> None`

取消注册监听器。


### `on_start(cb: py::function) -> None`

设置静态 eye-start 回调（替换之前的回调）。


### `on_complete(cb: py::function) -> None`

设置静态 eye-complete 回调（替换之前的回调）。


### `on_abort(cb: py::function) -> None`

设置静态 eye-abort 回调（替换之前的回调）。


### `clear_listeners() -> None`

注销所有静态回调并清除保存的 Python 函数。


### `init(eye_color: ColorCode, bg_color: ColorCode) -> int`

初始化眼睛子系统（LCD + 眼睛资源）。

**参数**

- **eye_color**: 默认虹膜颜色（参见 `Color.h`）。
- **bg_color**: 默认背景颜色（参见 `Color.h`）。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : already initialized - -1 : loadEyeFiles failed - -2 : LCD init failed
```


### `abort() -> None`

中止/终止活动动画（用于停止当前动画）。


### `is_active() -> bool`, `is_animating() -> bool`

查询是否已初始化以及是否正在播放动画。


### `set_eyes(shape: IrisShape, iris_color: ColorCode, bg_color: ColorCode) -> int`

将虹膜和背景设置为内置预设。

**返回值与错误代码请参见英文文档说明。**


### `set_iris(...)`, `get_iris_position(...)`, `set_background(...)`, `set_iris_image(...)`, `set_lid_image(...)`, `set_background_image(...)`, `set_animation(...)`, `set_position(...)`, `dispose() -> None`, `get_version() -> float`

其余函数与英文 API 描述对应，保留代码签名不变，返回码说明与英文文档一致。