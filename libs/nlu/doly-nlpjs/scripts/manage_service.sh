#!/bin/bash

# NLU Service Management Script (NLP.js)
# 用于自动化部署、管理 NLP.js Node.js 服务

set -e

SERVICE_NAME="doly-nlu"
SERVICE_FILE="doly-nlu.service"
SERVICE_PATH="/etc/systemd/system/$SERVICE_FILE"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BASE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
LOG_DIR="/home/pi/dolydev/logs"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

check_node() {
    print_status "检查 Node.js 环境..."
    if ! command -v node >/dev/null 2>&1; then
        print_error "Node.js 未安装"
        return 1
    fi
    node_version=$(node -v)
    print_success "Node.js 版本: $node_version"
    return 0
}

install_deps() {
    print_status "正在安装 npm 依赖..."
    cd "$BASE_DIR"
    if [ -f "package.json" ]; then
        npm install
        print_success "依赖安装完成"
    else
        print_error "package.json 未找到"
        return 1
    fi
}

train_model() {
    print_status "正在训练 NLU 模型..."
    cd "$BASE_DIR"
    if [ -f "train.js" ]; then
        node train.js
        print_success "模型训练完成"
    else
        print_error "train.js 未找到"
        return 1
    fi
}

install_service() {
    print_status "安装 NLU systemd 服务..."
    if [ ! -f "$BASE_DIR/$SERVICE_FILE" ]; then
        print_error "服务文件不存在: $BASE_DIR/$SERVICE_FILE"
        return 1
    fi
    
    # 修改服务文件中的路径（如果需要）
    # 这里假设服务文件已经正确配置，或者我们根据 BASE_DIR 动态调整
    sudo cp "$BASE_DIR/$SERVICE_FILE" "$SERVICE_PATH"
    sudo systemctl daemon-reload
    sudo systemctl enable "$SERVICE_NAME"
    print_success "服务已安装并设置为开机自启"
}

uninstall_service() {
    print_status "卸载 NLU 服务..."
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        sudo systemctl stop "$SERVICE_NAME"
    fi
    sudo systemctl disable "$SERVICE_NAME"
    [ -f "$SERVICE_PATH" ] && sudo rm "$SERVICE_PATH"
    sudo systemctl daemon-reload
    print_success "服务已卸载"
}

start_service() {
    print_status "启动 NLU 服务..."
    sudo systemctl start "$SERVICE_NAME"
    sleep 2
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "服务启动成功"
    else
        print_error "服务启动失败，请检查日志"
        show_logs
        return 1
    fi
}

stop_service() {
    print_status "停止 NLU 服务..."
    sudo systemctl stop "$SERVICE_NAME"
    print_success "服务已停止"
}

restart_service() {
    print_status "重启 NLU 服务..."
    sudo systemctl restart "$SERVICE_NAME"
    sleep 2
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "服务重启成功"
    else
        print_error "服务重启失败"
        return 1
    fi
}

show_status() {
    echo "=== 服务状态 ==="
    sudo systemctl status "$SERVICE_NAME" --no-pager -l || true
    echo
    echo "=== API 健康检查 ==="
    curl -s http://localhost:3000/health | python3 -m json.tool || echo "API 未能响应"
}

show_logs() {
    echo "=== 服务最近日志 ==="
    sudo journalctl -u "$SERVICE_NAME" -n 100 --no-pager
}

monitor_logs() {
    print_status "实时监控日志 (Ctrl+C 退出)..."
    sudo journalctl -u "$SERVICE_NAME" -f
}

reload_config() {
    print_status "重载服务配置..."
    sudo systemctl reload "$SERVICE_NAME" 2>/dev/null || {
        print_warning "服务不支持 reload，自动重启..."
        restart_service
    }
}

redeploy() {
    print_status "重新部署 NLU 服务: 安装依赖(可选)、替换 service 文件并重启"

    # 尝试安装/更新 npm 依赖（非致命）
    if [ -f "$BASE_DIR/package.json" ]; then
        print_status "安装/更新 npm 依赖..."
        (cd "$BASE_DIR" && npm install) || {
            print_warning "npm install 失败，继续尝试替换 service 文件"
        }
    else
        print_warning "package.json 未找到，跳过依赖安装"
    fi

    if [ ! -f "$BASE_DIR/$SERVICE_FILE" ]; then
        print_error "服务文件不存在: $BASE_DIR/$SERVICE_FILE"
        return 1
    fi

    # 若服务正在运行，先停止以便安全替换文件
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_status "服务正在运行，先停止以便替换文件"
        sudo systemctl stop "$SERVICE_NAME"
        sleep 1
    fi

    print_status "复制 $BASE_DIR/$SERVICE_FILE 到 $SERVICE_PATH"
    sudo cp "$BASE_DIR/$SERVICE_FILE" "$SERVICE_PATH"
    sudo systemctl daemon-reload

    print_status "重启服务"
    sudo systemctl restart "$SERVICE_NAME"
    sleep 2
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "重新部署成功，服务已运行"
    else
        print_error "重新部署失败: 服务未运行"
        show_logs
        return 1
    fi
}

show_help() {
    echo "Doly NLU 服务管理脚本"
    echo
    echo "用法: $0 [命令]"
    echo
    echo "命令:"
    echo "  init        安装依赖并训练模型"
    echo "  train       训练模型"
    echo "  install     安装 systemd 服务"
    echo "  uninstall   卸载 systemd 服务"
    echo "  start       启动服务"
    echo "  stop        停止服务"
    echo "  restart     重启服务"
    echo "  status      查看服务状态"
    echo "  logs        查看最近日志"
    echo "  monitor     实时查看日志"
    echo "  reload      重载服务配置（若服务不支持则重启）"
    echo "  redeploy    重新部署: 可选安装依赖、替换 service 文件并重启"
    echo "  help        显示帮助信息"
}

case "${1:-help}" in
    init)
        check_node
        install_deps
        train_model
        ;;
    train)
        train_model
        ;;
    install)
        install_service
        ;;
    uninstall)
        uninstall_service
        ;;
    start)
        start_service
        ;;
    stop)
        stop_service
        ;;
    restart)
        restart_service
        ;;
    reload)
        reload_config
        ;;
    redeploy)
        redeploy
        ;;
    status)
        show_status
        ;;
    logs)
        show_logs
        ;;
    monitor)
        monitor_logs
        ;;
    help)
        show_help
        ;;
    *)
        show_help
        exit 1
        ;;
esac
