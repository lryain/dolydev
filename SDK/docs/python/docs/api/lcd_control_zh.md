# LcdControl API 参考

导入：

```python
import doly_lcd
```

本页记录 `doly_lcd` Python 模块公开的 API。

## 枚举

### `LcdColorDepth`

取值：

- `L12BIT`
- `L18BIT`

### `LcdSide`

取值：

- `Left`
- `Right`

## 函数

### `init(depth: LcdColorDepth = LcdColorDepth::L12BIT) -> int`

初始化 LCD 设备。

**参数**

- **depth**: 配置的 LCD 色深。（默认：`LcdColorDepth::L12BIT`）

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : already initialized - -1 : open device failed - -2 : ioctl failed
```


### `dispose() -> int`

释放资源并反初始化 LCD 设备。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : already closed or not opened
```


### `is_active() -> bool`

检查 LCD 子系统是否已初始化。


### `lcd_color_fill(side: LcdSide, r: int, g: int, b: int) -> None`

使用纯 RGB 颜色填充面板。


### `write_lcd(side: LcdSide, buffer: py::buffer) -> int`

将缓冲区写入 LCD 内存。

**返回**

- `int` 状态码（0 成功）。


### `get_buffer_size() -> int`, `get_color_depth() -> LcdColorDepth`, `set_brightness(value: int) -> None`, `to_lcd_buffer(input: py::buffer, input_rgba: bool = False) -> bytes`, `get_version() -> float`

其它函数与英文文档描述一致，包含缓冲区大小、色深查询、亮度设置、缓冲区格式转换以及版本号获取。