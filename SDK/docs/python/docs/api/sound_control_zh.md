# SoundControl API 参考

导入：

```python
import doly_sound
```

本页记录 `doly_sound` Python 模块公开的 API。

## 枚举

### `SoundState`

取值：

- `Set`
- `Stop`
- `Play`

## 类

### `SoundEventListener`

监听器接口。实现此类并通过 `add_listener()` 注册以接收事件。

**方法**

- `onSoundBegin(id: int, volume: float) -> None`
  - 播放开始时调用。
- `onSoundComplete(id: int) -> None`
  - 播放完成时调用。
- `onSoundAbort(id: int) -> None`
  - 播放被中止时调用。
- `onSoundError(id: int) -> None`
  - 播放出错时调用。

## 函数

### `add_listener(listener: SoundEventListener, priority: bool = False) -> None`

注册 `SoundEventListener` 实例。

### `remove_listener(listener: SoundEventListener) -> None`

注销监听器。

### `on_begin(cb: py::function) -> None`, `on_complete(cb: py::function) -> None`, `on_abort(cb: py::function) -> None`, `on_error(cb: py::function) -> None`

设置静态回调（替换之前的同类回调）。

### `clear_listeners() -> None`

注销所有静态回调并清除保存的 Python 函数引用。

### `init() -> int`

初始化声音控制模块。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : already running - -1 : loading failed
```


### `dispose() -> int`

释放声音子系统并停止播放。


### `play(file_name: str, block_id: int) -> int`

开始播放音频文件（非阻塞）。

**参数**

- **file_name**: 音频文件路径。
- **block_id**: 用户定义的音频标识，会透传给事件回调。

**返回**

- `int`:

```text
Status code: - 0 : success - -1 : sound control not initialized - -2 : file not found
```


### `abort() -> None`, `get_state() -> SoundState`, `is_active() -> bool`, `set_volume(volume: int) -> int`, `get_version() -> float`

其余函数的语义与英文 API 一致。