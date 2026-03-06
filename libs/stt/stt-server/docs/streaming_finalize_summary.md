# 流式最终化总结

## 背景
- WebSocket 流式识别端点之前在每个 VAD 触发的片段结束时就把 `finished` 置为 true 并直接返回结果，因此客户端只能拿到第一段（例如“昨天”）。
- `sherpa-onnx` 命令行工具已经能输出整句，这说明流式路径也应该保持 Zipformer 模型的完整输出。

## 关键改动
1. `ASRSession` 现在除了继续调用共享引擎，还把每一块音频追加到 `full_utterance_buffer`，并增加 `request_finalize()`，在客户端发出完成标记后让共享引擎把缓存的输出刷新出来。
2. `process_speech_segment_shared()` 把中间结果标记为未完成，只有真正的最终响应会把 `finished` 设为 true，这样 WebSocket 客户端可以继续监听最终文本。
3. WebSocket 服务端的 `/sttRealtime` 路由现在能解析 JSON 控制帧，检测到 `{"done": true}` 就对会话调用 `request_finalize()`，负责发送整句并记录 `send_final_result()`。

## 如何触发最终结果
1. 音频流完之后发送形如下面的控制消息：
   ```json
   {"done": true}
   ```
2. 保持连接：服务器会先返回上一段的中间结果（如“昨天”），然后通过 `send_final_result()` 把缓存的整句（例如“昨天是 MONDAY …”）发出。
3. 最终结果携带 `finished: true`，客户端可以据此确认识别完成。

## 验证步骤
- 重启重新编译后的 WebSocket 服务（依然使用 `sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20` 模型资源），日志能看到中间结果和最终结果两次输出。
- 在 `/home/pi/DOLY-DIY/venv/bin/python test_client.py` 中访问 `ws://localhost:8001/sttRealtime`，观察到由中间结果+最终整句组成的两条消息。
- 用内联 Python WebSocket 脚本发送 `0.wav` 和 `{"done": true}` 控制帧，也能收到“昨天”然后“昨天是 MONDAY …”的完整输出。

## 说明
- 只有客户端明确发出 `done` 控制帧，服务端才会执行最终化，省略此操作时会一直输出临时假设。
- 所有后续接入该端点的客户端都应在收到首条消息后继续监听，直到拿到带 `finished` 的最终文本。
