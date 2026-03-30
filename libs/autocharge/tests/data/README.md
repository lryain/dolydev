失败样本目录

用处：保存 `autocharge_service` 运行时保存的调试帧（binary, roi, raw, debug），用于离线排查 ArUco 解码失败原因。

放置方式：运行工具 `libs/autocharge/tools/collect_failed_samples.sh` 会把 `/tmp/autocharge_debug` 下的样本复制到此处并生成 `index.txt`。

注意：此目录用于排查，不作为正式测试输入。若确认为有效样本，可移动到 `tests/data/positive` 或 `tests/data/negative` 以纳入单元测试。
