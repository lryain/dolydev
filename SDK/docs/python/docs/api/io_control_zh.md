# IoControl API 参考

导入：

```python
import doly_io
```

本页记录 `doly_io` Python 模块公开的 API。

## 枚举

### `GpioState`

取值：

- `Low`
- `High`

## 函数

### `write_pin(id: int, io_pin: int, state: GpioState) -> int`

向 IO 端口引脚写入 GPIO 状态。

**参数**

- **id**: 用户自定义标识，用于内部跟踪/日志（任意值）。
- **io_pin**: IO 端口引脚编号（有效范围：0..5）。
- **state**: 目标 GPIO 输出状态。

**返回**

- `int`:

```text
Status code: - 0 : success - -1 : invalid GPIO pin (must be IO port pin number 0..5) - -2 : GPIO write error
```


### `read_pin(id: int, io_pin: int) -> GpioState`

读取 IO 端口引脚的当前 GPIO 状态。

**参数**

- **id**: 用户自定义标识（任意值）。
- **io_pin**: IO 端口引脚编号（有效范围：0..5）。

**返回**

- `GpioState`:

```text
Current GPIO state. If @p io_pin is invalid or a read error occurs, the returned value is implementation-defined (see underlying GPIO layer).
```


### `get_version() -> float`

获取库版本（格式 0.XYZ）。