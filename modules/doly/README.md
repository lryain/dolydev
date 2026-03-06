# Doly Daemon 模块

Doly 主程序模块，负责统一控制 Doly 机器人。

## 功能

- 状态机管理（IDLE/ACTIVATED/EXPLORING/SLEEPING）
- 事件总线（传感器/语音/系统事件）
- 语音指令映射到动画/动作
- Blockly 代码执行
- 自主模式行为

## 目录结构

```
doly/
├── __init__.py          # 模块初始化
├── daemon.py            # 主程序入口
├── state_machine.py     # 状态机
├── event_bus.py         # 事件总线
├── command_mapper.py    # 命令映射器
├── blockly_executor.py  # Blockly 执行器
├── autonomous.py        # 自主模式行为
├── interfaces/          # 接口定义
│   ├── __init__.py
│   ├── event_interface.py
│   └── sensor_interface.py
└── tests/               # 测试
    ├── __init__.py
    ├── test_daemon.py
    └── test_event_bus.py
```

## 使用方法

```bash
# 启动 Daemon
python3 -m modules.doly.daemon

# 或直接运行
cd /home/pi/dolydev
python3 modules/doly/daemon.py
```

## 配置文件

- `config/voice_command_mapping.yaml` - 语音指令映射配置
- `config/system.yaml` - 系统配置

## 依赖服务

启动前需要确保以下服务运行：
- eyeEngine
- drive_service
- audio_player
- serial_service

## 许可

GPL-v3

## 作者

Kevin.Liu @ Make&Share
47129927@qq.com

## 更新日志

### v1.0.0 (2026-01-27)
- 初始版本发布
