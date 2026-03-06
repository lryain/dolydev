#!/bin/bash

# 风扇控制 - 命令速查表

# ============================================
# 模式控制
# ============================================

# 切换到持久quiet模式（一直保持）
# python3 tests/send_fan_cmd.py persistent-mode --mode quiet

# 切换到临时quiet模式（3秒后回到自动）
# python3 tests/send_fan_cmd.py mode --mode quiet --duration_ms 3000

# 切换到性能模式
# python3 tests/send_fan_cmd.py persistent-mode --mode performance

# 清除模式，回到自动温控
# python3 tests/send_fan_cmd.py clear-mode

# ============================================
# 静音禁止（唤醒词检测时用）
# ============================================

# 开启静音禁止（5秒）
# python3 tests/send_fan_cmd.py inhibit --id audio --duration_ms 5000

# 解除静音禁止
# python3 tests/send_fan_cmd.py uninhibit --id audio

# ============================================
# 手动控制
# ============================================

# 设置手动PWM值
# python3 tests/send_fan_cmd.py set_pwm --pwm 2000

# 设置百分比控制（50%转速）
# python3 tests/send_fan_cmd.py set_pwm --pct 0.5

# 全局禁用风扇
# python3 tests/send_fan_cmd.py enable --enabled 0

# 全局启用风扇
# python3 tests/send_fan_cmd.py enable --enabled 1

# ============================================
# 状态查询
# ============================================

# 订阅实时状态
# python3 tests/sub_fan_status.py

# ============================================
# 测试脚本
# ============================================

# 运行综合测试
# ./tests/test_persistent_mode.sh

# ============================================
# 服务管理
# ============================================

# 启动服务（前台运行，可看日志）
# ./run_service.sh

# 停止服务
# pkill -f "fan_service"

# 重新加载配置（不重启服务）
# kill -HUP $(pgrep -f "fan_service")
