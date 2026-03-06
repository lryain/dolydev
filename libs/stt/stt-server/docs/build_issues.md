# 编译问题总结

## 1. 问题概述
- 初次 `./build.sh` 时链接失败，错误信息集中在 `libsherpa-onnx-c-api.a` 无法解析 `sherpa_onnx` 命名空间的实现，后续 `libsherpa-onnx-cxx-api.a` 和 `libsherpa-onnx-core.a` 也无法解析彼此依赖的符号。
- 这是因为 `websocket_asr_server` 直接只链接了 API 库，缺失了 `core`、`fst` 系列、kaldi/knf/sentencepiece 等依赖。
- 链接库之间存在循环引用（C API 依赖 C++ API，C++ API 又依赖 C API 和 core），如果没有 `--start-group/--end-group`，`ld` 无法正确解决符号。

## 2. 解决办法
1. 扩充 `CMakeLists.txt` 中的 `find_library`，除了 C API/C++ API/core，还查找：`sherpa-onnx-fst`、`sherpa-onnx-fstfar`、`sherpa-onnx-kaldifst-core`、`libkaldi-decoder-core`、`libkaldi-native-fbank-core`、`libssentencepiece_core`、`libespeak-ng`、`libpiper_phonemize`、`libkissfft-float`、`libucd`、`libgflags`，以及已存在的 `libonnxruntime`，并确保它们都存在后才认为检索成功。
2. 将 `target_link_libraries` 中的 `SHERPA_ONNX_LIBRARIES` 放在 `-Wl,--start-group`/`-Wl,--end-group` 之间，保证静态库的交叉引用能被 `ld` 处理，同时保持 `jsoncpp`、`Threads` 与 `Boost` 的关键字链接风格一致。
3. `CMakeLists.txt` 给出了缺失部件的清单，以便追踪哪部分依赖没安装。

## 3. 验证
- 重新运行 `./build.sh` 成功完成，生成可执行文件 `build/websocket_asr_server`。
- 运行 `python example.py`（使用 `LD_LIBRARY_PATH` 指向编译产物和 SDK lib）也通过，说明动态库路径与依赖也满足。

## 4. 后续建议
- 若部署环境没有安装全部依赖，可参考项目根目录的 `install_sherpa_onnx.sh` 或 `install_sherpa_onnx_docker.sh` 进行安装。
- 编译脚本可以记录所需的 `LD_LIBRARY_PATH`，并在 `README` 中加上 linker 顺序注意事项。
