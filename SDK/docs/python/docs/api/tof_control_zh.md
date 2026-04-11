# TofControl API 参考

导入：

```python
import doly_tof
```

本页记录 `doly_tof` Python 模块公开的 API。

## 枚举

### `TofError`

取值示例：

- `NoError`
- `VcselContinuityTest`
- `VcselWatchdogTest`
- `Pll1Lock`
- `Pll2Lock`
- `EarlyConvergenceEstimate`
- `MaxConvergence`
- `NoTargetIgnore`
- `MaxSignalToNoiseRatio`
- `RawRangingAlgoUnderflow`
- `RawRangingAlgoOverflow`
- `RangingAlgoUnderflow`
- `RangingAlgoOverflow`
- `FilteredByPostProcessing`
- `DataNotReady`

### `TofSide`

取值：

- `Left`
- `Right`

### `TofGestureType`

取值：

- `Undefined`
- `ObjectComing`
- `ObjectGoing`
- `Scrubing`
- `ToLeft`
- `ToRight`

## 类

### `TofGesture`

**字段**

- **type**
- **range_mm**

### `TofData`

**字段**

- **update_ms**
- **range_mm**
- **error**
- **side**

### `TofEventListener`

监听器接口。实现并注册以接收事件。

**方法**

- `onProximityGesture(left: TofGesture, right: TofGesture) -> None`
  - 当检测到接近手势时调用。
- `onProximityThreshold(left: TofData, right: TofData) -> None`
  - 当达到接近阈值时调用（如启用）。

## 函数

### `add_listener(listener: TofEventListener, priority: bool = False) -> None`

注册监听器实例。

### `remove_listener(listener: TofEventListener) -> None`

取消注册监听器。

### `on_proximity_gesture(cb: py::function) -> None`, `on_proximity_threshold(cb: py::function) -> None`, `clear_listeners() -> None`

设置静态回调或清除回调。

### `init(offset_left: int = 0, offset_right: int = 0) -> int`

初始化 ToF 传感器。

**参数**

- **offset_left**, **offset_right**: 对左右传感器读数应用的偏移值。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : already running - -1 : both sensor init failed - -2 : left sensor init failed - -3 : right sensor init failed
```


### `dispose() -> int`, `setup_continuous(interval_ms: int = 50, distance: int = 0) -> int`, `get_sensors_data() -> list[TofData]`, `is_active() -> bool`, `is_reading() -> bool`, `get_version() -> float`

`setup_continuous()` 用于配置连续读取以便手势检测和阈值事件；返回码和使用建议请参考英文文档。