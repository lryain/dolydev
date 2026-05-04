#!/bin/bash

# Vision Service Management Script
# 用于自动化编译、部署、重启、查看状态等维护操作

set -e

SERVICE_NAME="vision-service"
SERVICE_FILE="vision-service.service"
SERVICE_PATH="/etc/systemd/system/$SERVICE_FILE"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/../build"
SERVICE_BINARY="$BUILD_DIR/LiveFaceReco"
TARGET_BIN="/usr/local/bin/vision_service"
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

check_dependencies() {
    print_status "检查依赖..."
    if [ ! -d "$LOG_DIR" ]; then
        print_warning "日志目录不存在，自动创建: $LOG_DIR"
        mkdir -p "$LOG_DIR"
    fi
    if [ ! -f "$SERVICE_BINARY" ]; then
        print_error "服务二进制文件不存在: $SERVICE_BINARY"
        print_status "请先运行 build 或将二进制放到 $SERVICE_BINARY"
        return 1
    fi
    print_success "依赖检查通过"
    return 0
}

build_service() {
    print_status "构建 Vision Service..."
    (cd "$SCRIPT_DIR/.." && mkdir -p build && cd build && cmake .. && cmake --build . -j2)
}

install_service() {
    print_status "安装 Vision systemd 服务..."
    if ! check_dependencies; then return 1; fi
    sudo cp "$SCRIPT_DIR/../scripts/$SERVICE_FILE" "$SERVICE_PATH"
    sudo systemctl daemon-reload
    sudo systemctl enable "$SERVICE_NAME"
    print_success "服务已安装并设置为开机自启"
}

uninstall_service() {
    print_status "卸载 Vision 服务..."
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        sudo systemctl stop "$SERVICE_NAME"
    fi
    sudo systemctl disable "$SERVICE_NAME" || true
    [ -f "$SERVICE_PATH" ] && sudo rm "$SERVICE_PATH"
    sudo systemctl daemon-reload
    print_success "服务已卸载"
}

start_service() {
    print_status "启动 Vision 服务..."
    if ! check_dependencies; then return 1; fi
    sudo systemctl start "$SERVICE_NAME"
    sleep 2
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "服务启动成功"
        show_status
    else
        print_error "服务启动失败"
        show_logs
        return 1
    fi
}

stop_service() {
    print_status "停止 Vision 服务..."
    sudo systemctl stop "$SERVICE_NAME"
    print_success "服务已停止"
}

restart_service() {
    print_status "重启 Vision 服务..."
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
    echo
    echo "=== 服务状态 ==="
    sudo systemctl status "$SERVICE_NAME" --no-pager -l
    echo
    echo "=== 最近日志 ==="
    sudo journalctl -u "$SERVICE_NAME" -n 10 --no-pager
}

show_logs() {
    echo "=== 服务最近日志 (非实时) ==="
    sudo journalctl -u "$SERVICE_NAME" -n 200 --no-pager
}

monitor_service() {
    print_status "实时监控服务日志 (Ctrl+C 退出)..."
    sudo journalctl -u "$SERVICE_NAME" -f
}

reload_config() {
    print_status "重载服务配置..."
    sudo systemctl reload "$SERVICE_NAME" 2>/dev/null || {
        print_warning "服务不支持 reload，自动重启..."
        restart_service
    }
}

# deploy 命令：如果已有二进制则跳过构建，否则构建再部署
# redeploy 命令则始终构建

deploy_service() {
    print_status "部署 Vision 服务: 检查二进制并更新"
    if [ -f "$SERVICE_BINARY" ]; then
        print_status "已有二进制 $SERVICE_BINARY，跳过构建"
    else
        print_status "未发现二进制，执行构建"
        build_service
        if [ ! -f "$SERVICE_BINARY" ]; then
            print_error "构建后仍未生成二进制: $SERVICE_BINARY"
            exit 1
        fi
    fi

    # 停止运行中的服务以便替换
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_status "服务运行中，停止以更新"
        sudo systemctl stop "$SERVICE_NAME"
        sleep 1
    fi

    print_status "更新 systemd 服务文件"
    sudo cp "$SCRIPT_DIR/$SERVICE_FILE" "$SERVICE_PATH"

    print_status "复制 $SERVICE_BINARY 到 $TARGET_BIN"
    sudo cp "$SERVICE_BINARY" "$TARGET_BIN"
    sudo chmod +x "$TARGET_BIN"

    print_status "重载 systemd 并重启服务"
    sudo systemctl daemon-reload
    sudo systemctl restart "$SERVICE_NAME"
    sleep 1
    show_status
}

redeploy_service() {
    print_status "重新部署 Vision 服务: 强制构建、复制并重启"
    build_service

    if [ ! -f "$SERVICE_BINARY" ]; then
        print_error "二进制不存在: $SERVICE_BINARY"
        exit 1
    fi

    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_status "服务运行中，先停止以便替换二进制"
        sudo systemctl stop "$SERVICE_NAME"
        sleep 1
    fi

    if ! sudo systemctl is-enabled --quiet "$SERVICE_NAME"; then
        print_status "检测到服务未被 enable，正在 enable"
        if sudo systemctl enable "$SERVICE_NAME"; then
            print_success "服务已 enable，会在启动时自动运行"
        else
            print_warning "无法 enable 服务，请检查权限或 unit 文件"
        fi
    fi
    
    print_status "更新 systemd 服务文件: $SERVICE_FILE -> $SERVICE_PATH"
    sudo cp "$SCRIPT_DIR/../scripts/$SERVICE_FILE" "$SERVICE_PATH"

    print_status "复制 $SERVICE_BINARY 到 $TARGET_BIN"
    sudo cp "$SERVICE_BINARY" "$TARGET_BIN"
    sudo chmod +x "$TARGET_BIN"

    print_status "重载 systemd 并重启服务"
    sudo systemctl daemon-reload
    sudo systemctl restart "$SERVICE_NAME"
    sleep 1
    show_status
}

show_help() {
    echo "Vision 服务管理脚本"
    echo
    echo "用法: $0 [命令]"
    echo
    echo "命令:"
    echo "  build       构建服务二进制"
    echo "  install     安装 systemd 服务"
    echo "  uninstall   卸载 systemd 服务"
    echo "  start       启动服务"
    echo "  stop        停止服务"
    echo "  restart     重启服务"
    echo "  status      查看服务状态和最近日志"
    echo "  logs        查看最近日志"
    echo "  monitor     实时监控服务日志"
    echo "  reload      重载服务配置"
    echo "  deploy      如果已有二进制则跳过构建，否则构建再部署"
    echo "  redeploy    重新构建/复制并重启服务"
    echo "  help        显示帮助信息"
}

case "${1:-help}" in
    build)
        build_service
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
    status)
        show_status
        ;;
    logs)
        show_logs
        ;;
    monitor)
        monitor_service
        ;;
    reload)
        reload_config
        ;;
    deploy)
        deploy_service
        ;;
    redeploy)
        redeploy_service
        ;;
    help|*)
        show_help
        ;;
esac
