# Camera API 参考

导入：

```python
import doly_camera
```

本页记录 `doly_camera` Python 模块公开的 API。

## 枚举

### `ExposureModes`

取值：

- `EXPOSURE_NORMAL`
- `EXPOSURE_SHORT`
- `EXPOSURE_CUSTOM`

### `MeteringModes`

取值：

- `METERING_CENTRE`
- `METERING_SPOT`
- `METERING_MATRIX`
- `METERING_CUSTOM`

### `WhiteBalanceModes`

取值：

- `WB_AUTO`
- `WB_NORMAL`
- `WB_INCANDESCENT`
- `WB_TUNGSTEN`
- `WB_FLUORESCENT`
- `WB_INDOOR`
- `WB_DAYLIGHT`
- `WB_CLOUDY`
- `WB_CUSTOM`

## 类

### `Options`

**字段**

- **help**
- **version**
- **list_cameras**
- **verbose**
- **timeout**
- **photo_width**
- **photo_height**
- **video_width**
- **video_height**
- **roi_x**
- **roi_y**
- **roi_width**
- **roi_height**
- **shutter**
- **gain**
- **ev**
- **awb_gain_r**
- **awb_gain_b**
- **brightness**
- **contrast**
- **saturation**
- **sharpness**
- **framerate**
- **denoise**
- **info_text**
- **camera**

**方法**

- `set_metering(mode: Any) -> None`
- `set_white_balance(mode: Any) -> None`
- `set_exposure_mode(mode: Any) -> None`
- `get_exposure_mode() -> None`
- `get_metering_mode() -> None`
- `get_white_balance() -> None`

### `PiCamera`

**方法**

- `start_photo(self: PiCamera) -> bool`
- `capture_photo(self: PiCamera) -> py::object`
- `stop_photo(self: PiCamera) -> bool`
- `start_video(self: PiCamera) -> bool`
- `get_video_frame(timeout_ms: PiCamera = 1500) -> py::object`
- `stop_video(self: PiCamera) -> None`
- `apply_zoom_options(self: PiCamera) -> None`
- `set_exposure(value: PiCamera) -> None`
- `set_awb_enable(enable: PiCamera) -> None`

## 函数

（模块提供若干函数以控制相机、捕获图像/视频帧并配置参数，详见绑定实现。）
