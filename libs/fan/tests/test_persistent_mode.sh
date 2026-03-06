#!/bin/bash

# 综合测试脚本：演示持久模式和临时模式的差异
# 注意：服务使用 bind，客户端使用 connect

cd /home/pi/dolydev/libs/fan

echo "========================================="
echo "测试 1：切换到持久的quiet模式"
echo "预期：模式保持quiet，不被温度覆盖"
echo "========================================="
python3 test/send_fan_cmd.py persistent-mode --mode quiet
sleep 2
echo ""

echo "========================================="
echo "测试 2：切换到持久的normal模式"
echo "预期：模式切换为normal，之前的quiet被覆盖"
echo "========================================="
python3 test/send_fan_cmd.py persistent-mode --mode normal
sleep 2
echo ""

echo "========================================="
echo "测试 3：临时quiet模式，3秒后回到自动"
echo "预期：3秒内保持quiet，3秒后恢复自动控制"
echo "========================================="
python3 test/send_fan_cmd.py mode --mode quiet --duration_ms 3000
echo "等待模式过期..."
sleep 4
echo ""

echo "========================================="
echo "测试 4：清除模式，回到自动控制"
echo "预期：模式被清除，风扇回到温度控制"
echo "========================================="
python3 test/send_fan_cmd.py clear-mode
sleep 1
echo ""

echo "========================================="
echo "测试 5：静音禁止（优先级高于持久模式）"
echo "先设置持久quiet模式，再加static inhibit"
echo "预期：inhibit后风扇停止，即使有quiet模式也不动"
echo "========================================="
python3 test/send_fan_cmd.py persistent-mode --mode quiet
sleep 1
python3 test/send_fan_cmd.py inhibit --id audio --duration_ms 5000
echo "inhibit active - fan should be stopped for 5 seconds"
sleep 6
echo "inhibit expired - persistent mode should resume"
echo ""

echo "========================================="
echo "所有测试完成！"
echo "========================================="
