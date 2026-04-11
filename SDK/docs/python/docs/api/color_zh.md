# Color API 参考

导入：

```python
import doly_color
```

本页记录 `doly_color` Python 模块公开的 API。

## 枚举

### `ColorCode`

取值：

- `Black`
- `White`
- `Gray`
- `Salmon`
- `Red`
- `DarkRed`
- `Pink`
- `Orange`
- `Gold`
- `Yellow`
- `Purple`
- `Magenta`
- `Lime`
- `Green`
- `DarkGreen`
- `Cyan`
- `SkyBlue`
- `Blue`
- `DarkBlue`
- `Brown`

## 类

### `Color`

**字段**

- **r**: 返回字符串表示。
- **g**: 返回字符串表示。
- **b**: 返回字符串表示。

**方法**

- `toString() -> str`
  - 返回字符串表示。
- `get_color(r: int, g: int, b: int) -> Color`
  - 由 r,g,b 创建一个 `Color` 实例。
- `hex_to_rgb(hex: str) -> Color`
  - 将十六进制字符串转换为 `Color`。
- `from_code(code: ColorCode) -> Color`
  - 根据 `ColorCode` 获取对应颜色。
- `get_led_color(code: ColorCode) -> Color`
  - 获取最接近的 LED 色调。
- `get_color_name(code: ColorCode) -> str`
  - 根据 `ColorCode` 获取颜色名称。
