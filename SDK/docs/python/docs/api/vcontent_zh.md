# VContent API 参考

导入：

```python
import doly_vcontent
```

本页记录 `doly_vcontent` Python 模块公开的 API。

## 类

### `VContent`

**字段**

- **active_frame_id**: 加载时选中的帧 ID。
- **ft**: 序列帧总数。
- **width**: 帧宽度。
- **height**: 帧高度。
- **path**: 源路径。
- **alpha**: 是否具有 alpha 通道。
- **color12Bit**: 是否为 12 位色（或更高）。
- **ratio**: 帧比例分割值。
- **loop**: 循环次数（0 表示无限循环）。
- **frames**: 原始帧数据（字节数组列表，用于调试/高级用途）。

**方法**

- `__init__() -> None`
  - 创建空的 `VContent`。
- `is_ready() -> bool`
  - 如果内容已成功加载则返回 True。
- `get_frame_bytes(index: int = 0) -> bytes`
  - 获取指定帧的原始字节（RGB/RGBA 缓冲）。
- `get_image(path: str, isRGBA: bool, set12Bit: bool) -> VContent`
  - 从路径加载 PNG 并返回 `VContent`。
    `isRGBA`: 若加载图像包含 alpha 通道，则为 True。
    `set12Bit`: 将图像转换为 12 位色深（若需要）。
