#!/bin/bash
#
# Doly-小智集成快速部署脚本
#
# 用法: ./deploy_xiaozhi_integration.sh [clean|build|test|all]
#

set -e  # 遇到错误立即退出

PROJECT_ROOT="/home/pi/dolydev"
XIAOZHI_DIR="$PROJECT_ROOT/xiaozhidev/xiaozhi_client"
BUILD_DIR="$XIAOZHI_DIR/build"

echo "=========================================="
echo " Doly-小智集成部署"
echo "=========================================="

# 函数：清理构建
clean_build() {
    echo ""
    echo "[1/4] 清理旧构建..."
    if [ -d "$BUILD_DIR" ]; then
        rm -rf "$BUILD_DIR"
        echo "  ✓ 已删除 build 目录"
    fi
}

# 函数：编译小智客户端
build_xiaozhi() {
    echo ""
    echo "[2/4] 编译小智客户端..."
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    
    echo "  - 运行 cmake..."
    cmake .. || {
        echo "  ✗ cmake 失败"
        exit 1
    }
    
    echo "  - 运行 make..."
    make -j2 || {
        echo "  ✗ make 失败"
        exit 1
    }
    
    echo "  ✓ 编译成功"
}

# 函数：检查 Python 模块
check_python_modules() {
    echo ""
    echo "[3/4] 检查 Python 模块..."
    cd "$PROJECT_ROOT"
    
    python3 -c "from modules.taskEngine import TaskEngine; print('  ✓ TaskEngine 可导入')" || {
        echo "  ✗ TaskEngine 导入失败"
        exit 1
    }
    
    python3 -c "from modules.doly.managers.xiaozhi_manager import XiaozhiManager; print('  ✓ XiaozhiManager 可导入')" || {
        echo "  ✗ XiaozhiManager 导入失败"
        exit 1
    }
    
    echo "  ✓ 所有 Python 模块正常"
}

# 函数：运行测试
run_tests() {
    echo ""
    echo "[4/4] 运行测试..."
    cd "$PROJECT_ROOT"
    
    echo "  - TaskEngine 单元测试..."
    python3 xiaozhidev/xiaozhi_client/scripts/test_task_engine.py > /dev/null 2>&1 && \
        echo "    ✓ TaskEngine 测试通过" || \
        echo "    ✗ TaskEngine 测试失败"
    
    echo "  - 情绪流转测试..."
    python3 xiaozhidev/xiaozhi_client/scripts/test_emotion_flow.py > /dev/null 2>&1 && \
        echo "    ✓ 情绪流转测试通过" || \
        echo "    ✗ 情绪流转测试失败"
    
    echo "  - 动作指令测试..."
    python3 xiaozhidev/xiaozhi_client/scripts/test_action_command.py > /dev/null 2>&1 && \
        echo "    ✓ 动作指令测试通过" || \
        echo "    ✗ 动作指令测试失败"
    
    echo "  - 意图执行测试..."
    python3 xiaozhidev/xiaozhi_client/scripts/test_intent_execution.py > /dev/null 2>&1 && \
        echo "    ✓ 意图执行测试通过" || \
        echo "    ✗ 意图执行测试失败"
}

# 函数：显示使用说明
show_usage() {
    cat << EOF

========================================
 部署完成！
========================================

下一步操作：

1. 启动 Daemon:
   cd $PROJECT_ROOT
   python3 modules/doly/daemon.py > logs/doly_daemon.log 2>&1 &

2. 启动小智客户端:
   cd $BUILD_DIR
   ./xiaozhi_client

3. 运行端到端测试:
   cd $PROJECT_ROOT
   python3 xiaozhidev/xiaozhi_client/scripts/test_e2e_integration.py

4. 查看日志:
   tail -f logs/doly_daemon.log

详细文档: DOLY_XIAOZHI_INTEGRATION_COMPLETE.md

EOF
}

# 主流程
case "${1:-all}" in
    clean)
        clean_build
        ;;
    build)
        build_xiaozhi
        check_python_modules
        ;;
    test)
        run_tests
        ;;
    all)
        clean_build
        build_xiaozhi
        check_python_modules
        run_tests
        show_usage
        ;;
    *)
        echo "用法: $0 [clean|build|test|all]"
        echo ""
        echo "  clean - 清理构建"
        echo "  build - 编译小智客户端"
        echo "  test  - 运行测试"
        echo "  all   - 执行所有步骤（默认）"
        exit 1
        ;;
esac

echo ""
echo "✅ 完成！"
