#!/bin/bash

# Widget Service Management Script
# 用于自动化编译、部署、重启、查看状态等维护操作

set -e

SERVICE_NAME="widget-service"
SERVICE_FILE="widget-service.service"
SERVICE_PATH="/etc/systemd/system/$SERVICE_FILE"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
WIDGET_BASE_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$WIDGET_BASE_DIR/build"
SERVICE_BINARY="$BUILD_DIR/widget_service"
CONFIG_FILE="$WIDGET_BASE_DIR/config/widgets.default.json"
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
    
    # 检查日志目录
    if [ ! -d "$LOG_DIR" ]; then
        print_warning "日志目录不存在，自动创建: $LOG_DIR"
        mkdir -p "$LOG_DIR"
    fi
    
    # 检查服务二进制
    if [ ! -f "$SERVICE_BINARY" ]; then
        print_error "服务二进制不存在: $SERVICE_BINARY"
        print_status "请先编译: cd $WIDGET_BASE_DIR && mkdir -p build && cd build && cmake .. && make -j2"
        return 1
    fi
    
    # 检查配置文件
    if [ ! -f "$CONFIG_FILE" ]; then
        print_error "配置文件不存在: $CONFIG_FILE"
        return 1
    fi
    
    print_success "依赖检查通过"
    return 0
}

build_service() {
    print_status "编译 Widget 服务..."
    
    if [ ! -d "$BUILD_DIR" ]; then
        print_status "创建编译目录: $BUILD_DIR"
        mkdir -p "$BUILD_DIR"
    fi
    
    cd "$BUILD_DIR"
    cmake ..
    make -j2
    
    print_success "编译完成"
}

install_service() {
    print_status "安装 Widget systemd 服务..."
    
    if ! check_dependencies; then 
        print_error "依赖检查失败，无法继续"
        return 1
    fi
    
    # 复制服务文件（从父目录）
    sudo cp "$WIDGET_BASE_DIR/scripts/$SERVICE_FILE" "$SERVICE_PATH"
    
    # 重新加载 systemd 配置
    sudo systemctl daemon-reload
    
    # 启用开机自启
    sudo systemctl enable "$SERVICE_NAME"
    
    print_success "Widget 服务已安装"
    print_status "使用 'systemctl start $SERVICE_NAME' 启动服务"
}

uninstall_service() {
    print_status "卸载 Widget 服务..."
    
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        sudo systemctl stop "$SERVICE_NAME"
    fi
    
    sudo systemctl disable "$SERVICE_NAME"
    
    if [ -f "$SERVICE_PATH" ]; then
        sudo rm "$SERVICE_PATH"
    fi
    
    sudo systemctl daemon-reload
    
    print_success "Widget 服务已卸载"
}

start_service() {
    print_status "启动 Widget 服务..."
    
    if ! check_dependencies; then 
        return 1
    fi
    
    sudo systemctl start "$SERVICE_NAME"
    sleep 3
    
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "Widget 服务启动成功"
        show_status
        return 0
    else
        print_error "Widget 服务启动失败"
        show_logs
        return 1
    fi
}

stop_service() {
    print_status "停止 Widget 服务..."
    
    sudo systemctl stop "$SERVICE_NAME"
    sleep 1
    
    print_success "Widget 服务已停止"
}

restart_service() {
    print_status "重启 Widget 服务..."
    
    if ! check_dependencies; then 
        return 1
    fi
    
    sudo systemctl restart "$SERVICE_NAME"
    sleep 3
    
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "Widget 服务重启成功"
        show_status
        return 0
    else
        print_error "Widget 服务重启失败"
        show_logs
        return 1
    fi
}

show_status() {
    print_status "Widget 服务状态："
    echo ""
    sudo systemctl status "$SERVICE_NAME" --no-pager || true
    echo ""
}

show_logs() {
    print_status "Widget 服务日志（最近 50 行）："
    echo ""
    sudo journalctl -u "$SERVICE_NAME" -n 50 --no-pager -q || true
    echo ""
}

watch_logs() {
    print_status "实时监看 Widget 服务日志（按 Ctrl+C 退出）..."
    echo ""
    sudo journalctl -u "$SERVICE_NAME" -f --no-pager 2>/dev/null || true
}

redeploy_service() {
    print_status "重新部署 Widget 服务..."
    
    # 停止现有服务
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        stop_service
    fi
    
    # 卸载旧服务
    if [ -f "$SERVICE_PATH" ]; then
        uninstall_service
    fi
    
    # 重新编译和安装
    build_service
    install_service
    sleep 1
    start_service
    
    print_success "Widget 服务已重新部署"
}

# 部署命令: 如已有二进制则直接安装/重启，否则先编译
# 与redeploy不同，deploy不会强制重新编译，只在需要时才构建

deploy_service() {
    print_status "部署 Widget 服务..."

    # 如果没有二进制，则先构建
    if [ ! -f "$SERVICE_BINARY" ]; then
        print_status "未发现二进制文件，开始编译"
        build_service
    else
        print_status "二进制存在，跳过编译"
    fi

    # 停止现有服务（如果正在运行）
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        stop_service
    fi

    # 卸载旧服务（如果已安装）
    if [ -f "$SERVICE_PATH" ]; then
        uninstall_service
    fi

    install_service
    sleep 1
    start_service

    print_success "Widget 服务已部署"
}

health_check() {
    print_status "检查 Widget 服务健康状态..."
    
    if ! sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_error "Widget 服务未运行"
        return 1
    fi
    
    # 检查服务 PID
    PID=$(sudo systemctl show -p MainPID --value "$SERVICE_NAME")
    if [ -z "$PID" ] || [ "$PID" == "0" ]; then
        print_error "无法获取服务 PID"
        return 1
    fi
    
    print_success "Widget 服务正在运行 (PID: $PID)"
    
    # 检查内存使用
    MEMORY=$(ps aux | awk -v pid=$PID '$2 == pid {printf "%.1f%%", $4}')
    print_status "内存使用率: $MEMORY"
    
    # 检查 CPU 使用
    CPU=$(ps aux | awk -v pid=$PID '$2 == pid {printf "%.1f%%", $3}')
    print_status "CPU 使用率: $CPU"
    
    # 检查最近错误
    ERRORS=$(sudo journalctl -u "$SERVICE_NAME" -n 10 --no-pager -q | grep -i "error" | wc -l)
    if [ $ERRORS -gt 0 ]; then
        print_warning "最近 10 条日志中发现 $ERRORS 条错误"
    else
        print_success "未发现错误日志"
    fi
    
    return 0
}

print_help() {
    cat << EOF
Widget Service Management Script

用法: $0 <command>

命令:
  build        - 编译 Widget 服务（可选，install 会自动检查）
  install      - 安装服务（需要 sudo）
  uninstall    - 卸载服务（需要 sudo）
  start        - 启动服务（需要 sudo）
  stop         - 停止服务（需要 sudo）
  restart      - 重启服务（需要 sudo）
  status       - 显示服务状态
  logs         - 显示最近的日志
  watch        - 实时监看日志
  deploy       - 部署服务（存在二进制则跳过编译，否则编译）
  redeploy     - 重新部署服务（编译 + 卸载 + 安装 + 启动，需要 sudo）
  health       - 检查服务健康状态
  help         - 显示此帮助信息

示例:
  $0 build       # 编译服务
  $0 install     # 安装服务
  $0 start       # 启动服务
  $0 watch       # 监看日志
  $0 health      # 检查健康状态

EOF
}

# ============================================================================
# Main
# ============================================================================

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
    watch)
        watch_logs
        ;;
    deploy)
        deploy_service
        ;;
    redeploy)
        redeploy_service
        ;;
    health)
        health_check
        ;;
    help)
        print_help
        ;;
    *)
        print_error "未知命令: $1"
        print_help
        exit 1
        ;;
esac
