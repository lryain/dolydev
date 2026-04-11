# FanControl API 参考

导入：

```python
import doly_fan
```

本页记录 `doly_fan` Python 模块公开的 API。

## 函数

### `init(auto_control: bool) -> int`

初始化风扇控制模块。

**参数**

- **auto_control**: 若为 `True`，启用自动温控以管理风扇转速。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : already initialized - <0 : error (implementation-defined)
```


### `dispose() -> int`

释放风扇控制模块资源并停止。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : not initialized - <0 : error
```


### `set_fan_speed(percentage: int) -> int`

按百分比设置风扇速度。

**参数**

- **percentage**: 风扇速度百分比 (0..100)。

**返回**

- `int`:

```text
Status code: - <0 : error
```

**注意**

- 若在 `init()` 时启用了 `auto_control`，自动控制器可能会覆盖或随时间调整设定的风扇速度。


### `is_active() -> bool`

检查风扇模块是否已初始化并激活。


### `get_version() -> float`

获取库版本（格式说明：0.XYZ）。