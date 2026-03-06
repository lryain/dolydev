# ASR 模型兼容性指南

## 概述

WebSocket ASR 服务器现已支持**两种模型架构的自动适配**：

| 模型 | 类型 | 文件结构 | 处理模式 | 元数据支持 |
|------|------|---------|---------|----------|
| **SenseVoice** | 离线 | model.onnx + tokens.txt | 完整音频 | ✅ 语言/情感/事件 |
| **Zipformer** | 流式 | encoder + decoder + joiner | 增量流 | ❌ 无特殊元数据 |

## 自动检测机制

启动服务器时，系统会**自动识别模型类型**：

```cpp
// 检测逻辑：
// 1. 检查是否存在 encoder-*.onnx 文件 → Zipformer
// 2. 否则检查是否存在 model.onnx → SenseVoice  
// 3. 手动指定 model_type 参数会覆盖自动检测
```

## 使用方式

### 1️⃣ SenseVoice 离线模型

**启动命令：**
```bash
# 方案1：自动检测（推荐）
./build/websocket_asr_server \
  --models-root /path/to/models \
  --asr-model "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17" \
  --asr-language "auto" \
  --port 8001

# 方案2：显式指定
./build/websocket_asr_server \
  --models-root /path/to/models \
  --asr-model "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17" \
  --asr-model-type "sense-voice" \
  --port 8001
```

**模型目录结构：**
```
models/
└── sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17/
    ├── model.onnx           # 必需
    └── tokens.txt           # 必需
```

**识别结果包含：**
- 识别文本 (text)
- 语言识别 (language) - 自动检测
- 情感识别 (emotion) - 如可用
- 事件检测 (event) - 如可用  
- 时间戳 (timestamps)
- Token 列表 (tokens)

### 2️⃣ Streaming Zipformer 流式模型

**启动命令：**
```bash
# 方案1：自动检测（推荐）
./build/websocket_asr_server \
  --models-root /path/to/models \
  --asr-model "sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20" \
  --port 8001

# 方案2：显式指定路径
./build/websocket_asr_server \
  --models-root /path/to/models \
  --asr-model-type "zipformer" \
  --encoder "/models/model/encoder-epoch-99-avg-1.int8.onnx" \
  --decoder "/models/model/decoder-epoch-99-avg-1.onnx" \
  --joiner "/models/model/joiner-epoch-99-avg-1.int8.onnx" \
  --port 8001

# 方案3：模型根目录自动查找（最简单）
./build/websocket_asr_server \
  --models-root /path/to/models \
  --asr-model "sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20" \
  --port 8001
```

**模型目录结构：**
```
models/
└── sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20/
    ├── encoder-epoch-99-avg-1.int8.onnx    # 必需
    ├── encoder-epoch-99-avg-1.onnx         # 可选
    ├── decoder-epoch-99-avg-1.onnx         # 必需
    ├── joiner-epoch-99-avg-1.int8.onnx     # 推荐
    ├── joiner-epoch-99-avg-1.onnx          # 可选
    └── tokens.txt                          # 必需
```

**识别结果包含：**
- 识别文本 (text)
- 其他字段为空（Zipformer 流式引擎不提供）

## 环境变量配置

除了命令行参数，也可通过环境变量配置：

```bash
# SenseVoice 配置
export ASR_MODEL_TYPE="sense-voice"
export ASR_MODEL_NAME="sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17"
export ASR_LANGUAGE="auto"
export ASR_USE_ITN="true"

# Zipformer 配置
export ASR_MODEL_TYPE="zipformer"
export ASR_MODEL_NAME="sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20"
export ASR_ENCODER_PATH="/path/to/encoder.onnx"
export ASR_DECODER_PATH="/path/to/decoder.onnx"
export ASR_JOINER_PATH="/path/to/joiner.onnx"

# 启动
./build/websocket_asr_server --models-root /path/to/models --port 8001
```

## API 兼容性

### 统一接口

两种模型使用**完全相同的 API 接口**：

```python
# Python 客户端示例
import asyncio
import websocket_client

async def recognize(audio_file, model_type):
    """
    params:
        audio_file: WAV 文件路径
        model_type: 'sense-voice' 或 'zipformer'
    """
    ws = await websocket_client.connect("ws://localhost:8001/sttRealtime")
    
    # 发送音频块（两种模型兼容）
    with open(audio_file, 'rb') as f:
        while chunk := f.read(4096):
            await ws.send_bytes(chunk)
    
    # 获取识别结果
    result = await ws.recv()
    print(f"识别结果：{result['text']}")
    
    # SenseVoice 支持额外的元数据
    if 'language' in result:
        print(f"检测语言：{result['language']}")
        print(f"情感：{result['emotion']}")
```

## 模型切换指南

### 快速切换模型

```bash
# 停止当前服务
pkill websocket_asr_server

# 切换到不同模型（自动检测）
./build/websocket_asr_server \
  --models-root /path/to/models \
  --asr-model "sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20" \
  --port 8001
```

### 模型验证

```bash
# 测试 SenseVoice
./build/websocket_asr_server \
  --models-root /path/to/models \
  --asr-model "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17" \
  --asr-debug \
  --port 8001

# 测试 Zipformer  
./build/websocket_asr_server \
  --models-root /path/to/models \
  --asr-model "sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20" \
  --asr-debug \
  --port 8001
```

## 常见问题

### Q: 如何判断加载了哪个模型？
A: 查看启动日志中的信息：
```
[SHARED_ASR] Detected Zipformer model from encoder file: encoder-epoch-99-avg-1.int8.onnx
```

### Q: 找不到 Zipformer 文件怎么办？
A: 检查模型目录结构包含 encoder、decoder、joiner 的 ONNX 文件，并验证文件名包含这些关键字（大小写敏感）。

### Q: 为什么 Zipformer 返回的结果没有语言信息？
A: Zipformer 是流式架构，不提供 SenseVoice 的语言/情感/事件元数据。这是设计特性。

### Q: 可以同时运行两个模型吗？
A: 建议在不同端口运行不同的 ASR 服务实例：
```bash
# 终端1：SenseVoice
./build/websocket_asr_server --asr-model "...sense-voice..." --port 8001

# 终端2：Zipformer
./build/websocket_asr_server --asr-model "...zipformer..." --port 8002
```

## 架构细节

### SenseVoice（离线）流程
```
输入音频 → OfflineStream → OfflineRecognizer → OfflineRecognizerResult
            ├─ text
            ├─ language
            ├─ emotion  
            └─ event
```

### Zipformer（流式）流程
```
输入音频 → OnlineStream → OnlineRecognizer → OnlineRecognizerResult
          ├─ AcceptWaveform  ├─ IsReady
          ├─ InputFinished   ├─ Decode
          └─ GetResult       └─ text (仅)
```

## 性能对比

| 指标 | SenseVoice | Zipformer |
|------|-----------|-----------|
| 延迟 | 完整音频后 | 极低（流式） |
| 内存 | 中 | 低 |
| 精度 | 高 | 高 |
| 多语言 | 支持 | 语言对可配 |
| 元数据 | 丰富 | 无 |

## 技术实现

### 检测逻辑 (detect_model_type)
```cpp
1. 遍历模型目录
2. 若找到 "encoder" *.onnx → ZIPFORMER
3. 若找到 model.onnx → SENSE_VOICE
4. 手动指定 model_type 会覆盖自动检测
```

### 初始化适配
```cpp
// SenseVoice: 使用 OfflineRecognizerConfig + sense_voice 字段
// Zipformer: 使用 OnlineRecognizerConfig + transducer 字段
```

### 识别适配
```cpp
// SenseVoice: OfflineStream, Decode(...), GetResult()
// Zipformer: OnlineStream, IsReady(), Decode(...), InputFinished()
```

## 下一步

### 计划支持的功能
- [ ] 动态模型切换（无需重启）
- [ ] 模型热加载
- [ ] 多模型并行处理
- [ ] 自定义 Zipformer 双语对

### 反馈与报告
如有问题，请查看日志中的详细信息，特别是初始化阶段的日志输出。
