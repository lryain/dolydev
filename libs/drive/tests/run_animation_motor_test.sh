#!/bin/bash
# 动画系统电机 API 完整测试脚本
# 自动启动 drive_service 并运行所有测试

set -e  # 遇到错误立即退出

echo "╔══════════════════════════════════════════════════════════════╗"
echo "║         动画系统电机 API 测试 - 自动化脚本                    ║"
echo "╚══════════════════════════════════════════════════════════════╝"
echo ""

# 工作目录
WORK_DIR="/home/pi/dolydev"
cd "$WORK_DIR"

# 1. 检查并启动 drive_service
echo "📋 步骤 1: 检查 drive_service 状态"
if pgrep -f "drive_service" > /dev/null; then
    echo "  ✅ drive_service 已运行"
    read -p "  是否重启? (y/N): " restart
    if [[ "$restart" =~ ^[Yy]$ ]]; then
        echo "  🔄 重启 drive_service..."
        sudo pkill -f -9 drive_service
        sleep 1
        cd "$WORK_DIR/libs/drive/build"
        ./drive_service > ../drive.log 2>&1 &
        sleep 2
        echo "  ✅ drive_service 已重启"
    fi
else
    echo "  ❌ drive_service 未运行"
    read -p "  是否启动? (Y/n): " start
    if [[ ! "$start" =~ ^[Nn]$ ]]; then
        echo "  🚀 启动 drive_service..."
        cd "$WORK_DIR/libs/drive/build"
        ./drive_service > ../drive.log 2>&1 &
        sleep 2
        if pgrep -f "drive_service" > /dev/null; then
            echo "  ✅ drive_service 启动成功"
        else
            echo "  ❌ drive_service 启动失败，请检查日志"
            exit 1
        fi
    else
        echo "  ⚠️  未启动 drive_service，测试可能失败"
    fi
fi

cd "$WORK_DIR"
echo ""

# 2. 显示测试菜单
echo "📋 步骤 2: 选择测试项目"
echo "─────────────────────────────────────────────────────────"
echo "  1) 基础移动控制 (前进/后退)"
echo "  2) 精确距离控制 (drive_distance)"
echo "  3) 精确转向控制 (drive_rotate_left)"
echo "  4) 手动转向控制 (时长模式)"
echo "  5) 手动速度控制 (左右轮独立)"
echo "  6) 编码器状态查询"
echo "  7) 编码器脉冲控制 (底层API)"
echo "  8) 动画场景模拟 (复合动作)"
echo "  a) 运行所有测试 (推荐)"
echo "  q) 退出"
echo "─────────────────────────────────────────────────────────"
read -p "请选择 [1-8/a/q]: " choice

case "$choice" in
    1)
        TEST_ARG="basic"
        TEST_NAME="基础移动控制"
        ;;
    2)
        TEST_ARG="distance"
        TEST_NAME="精确距离控制"
        ;;
    3)
        TEST_ARG="rotate"
        TEST_NAME="精确转向控制"
        ;;
    4)
        TEST_ARG="turn"
        TEST_NAME="手动转向控制"
        ;;
    5)
        TEST_ARG="speed"
        TEST_NAME="手动速度控制"
        ;;
    6)
        TEST_ARG="encoder"
        TEST_NAME="编码器状态查询"
        ;;
    7)
        TEST_ARG="pulse"
        TEST_NAME="编码器脉冲控制"
        ;;
    8)
        TEST_ARG="animation"
        TEST_NAME="动画场景模拟"
        ;;
    a|A)
        TEST_ARG=""
        TEST_NAME="所有测试"
        ;;
    q|Q)
        echo "  👋 退出测试"
        exit 0
        ;;
    *)
        echo "  ❌ 无效选择"
        exit 1
        ;;
esac

echo ""
echo "🚀 准备运行: $TEST_NAME"
echo ""

# 3. 安全提示
echo "⚠️  安全检查清单:"
echo "  □ Doly 放在安全的测试区域"
echo "  □ 确认周围没有障碍物"
echo "  □ 准备好紧急停止 (Ctrl+C)"
echo "  □ 打开另一个终端查看日志:"
echo "    tail -f $WORK_DIR/libs/drive/drive.log"
echo ""
read -p "确认准备就绪? (Y/n): " ready
if [[ "$ready" =~ ^[Nn]$ ]]; then
    echo "  ❌ 测试取消"
    exit 0
fi

echo ""
echo "─────────────────────────────────────────────────────────"

# 4. 运行测试
if [[ -z "$TEST_ARG" ]]; then
    # 运行所有测试
    python3 "$WORK_DIR/libs/drive/tests/test_animation_motor_api.py"
else
    # 运行单项测试
    python3 "$WORK_DIR/libs/drive/tests/test_animation_motor_api.py" "$TEST_ARG"
fi

TEST_EXIT_CODE=$?

echo "─────────────────────────────────────────────────────────"
echo ""

# 5. 测试结果
if [[ $TEST_EXIT_CODE -eq 0 ]]; then
    echo "✅ 测试完成"
else
    echo "❌ 测试失败 (退出码: $TEST_EXIT_CODE)"
fi

echo ""
echo "📊 查看详细日志:"
echo "  tail -50 $WORK_DIR/libs/drive/drive.log"
echo ""

# 6. 查看最近日志
read -p "是否查看最近 30 行日志? (y/N): " show_log
if [[ "$show_log" =~ ^[Yy]$ ]]; then
    echo ""
    echo "─────────────────────────────────────────────────────────"
    tail -30 "$WORK_DIR/libs/drive/drive.log"
    echo "─────────────────────────────────────────────────────────"
fi

echo ""
echo "👋 测试脚本结束"
