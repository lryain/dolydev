# TtsControl API 参考

导入：

```python
import doly_tts
```

本页记录 `doly_tts` Python 模块公开的 API。

## 枚举

### `VoiceModel`

取值：

- `Model1`
- `Model2`
- `Model3`

## 函数

### `init(model: VoiceModel, output_path: str = "") -> int`

初始化 TTS 系统。

**参数**

- **model**: 使用的语音模型。
- **output_path**: 可选的输出目录/路径（默认：""）。

**返回**

- `int`:

```text
Status code: - 0 : success - 1 : already initialized - -1 : model file missing - -2 : model config file missing
```

**注意**

- 模型加载是耗时且占 CPU 的阻塞操作。


### `dispose() -> int`, `produce(text: str) -> int`, `get_version() -> float`

`produce()` 将文本合成成语音并输出（阻塞操作）。返回码与英文文档一致。