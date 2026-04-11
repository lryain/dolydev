# Python SDK

本节记录 Doly Python SDK 的说明与使用指引。

## 概述

Doly Python SDK 是一组小而专注的模块（每个能力对应一个模块），基于 Doly 的 C++ SDK 并使用 **pybind11** 封装。

- **平台**：Raspberry Pi OS
- **Python**：3.11
- **分发**：预装在机器人上；开发者可从源码重建模块。

## 模块布局（常见）

每个模块通常包含：

- `example.py` — 最小可用示例
- `source/` — 构建系统（CMake + pyproject）与 pybind11 绑定源码

## 文档风格

每个模块页面包含：

- 导入说明
- 最小可用示例（来自 `example.py`）
- 常见任务 / 使用示例
- 备注（权限、服务、注意事项）
- 指向 API 参考页面的链接


## 链接

 - [Doly Shop](https://shop.doly.ai)
 - [Doly 网站](https://doly.ai)
 - [Doly 社区](https://community.doly.ai)
 - [GitHub 仓库](https://github.com/robotdoly/DOLY-DIY)
