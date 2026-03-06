#!/bin/bash
# EyeEngine ZMQ 服务快速启动脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

echo "========================================="
echo "  EyeEngine ZMQ 服务启动"
echo "========================================="
echo ""

# 检查参数
if [ "$1" == "--help" ] || [ "$1" == "-h" ]; then
    echo "用法: $0 [选项]"
    echo ""
    echo "选项:"
    echo "  --config <文件>    使用自定义配置文件"
    echo "  --mock             使用模拟驱动（调试用）"
    echo "  --debug            启用调试日志"
    echo "  --help, -h         显示此帮助"
    echo ""
    echo "示例:"
    echo "  $0                 # 使用默认配置启动"
    echo "  $0 --mock          # 模拟模式启动"
    echo "  $0 --config my.yaml --debug  # 自定义配置+调试"
    echo ""
    exit 0
fi

# 构建参数
ARGS=""

while [[ $# -gt 0 ]]; do
    case $1 in
        --config)
            ARGS="$ARGS --config $2"
            shift 2
            ;;
        --mock)
            ARGS="$ARGS --mock"
            shift
            ;;
        --debug)
            ARGS="$ARGS --log-level DEBUG"
            shift
            ;;
        *)
            echo "未知选项: $1"
            echo "使用 --help 查看帮助"
            exit 1
            ;;
    esac
done

# 检查 Python
if ! command -v python3 &> /dev/null; then
    echo "错误: 未找到 python3"
    exit 1
fi

# 检查依赖
python3 -c "import zmq" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "错误: 缺少 pyzmq 依赖"
    echo "请安装: pip3 install pyzmq"
    exit 1
fi

python3 -c "import yaml" 2>/dev/null
if [ $? -ne 0 ]; then
    echo "错误: 缺少 pyyaml 依赖"
    echo "请安装: pip3 install pyyaml"
    exit 1
fi

# 启动服务
echo "启动 EyeEngine ZMQ 服务..."
echo "命令端点: ipc:///tmp/doly_eye_cmd.sock"
echo "事件端点: ipc:///tmp/doly_eye_event.sock"
echo ""
echo "按 Ctrl+C 停止服务"
echo "========================================="
echo ""

exec python3 zmq_service.py $ARGS
