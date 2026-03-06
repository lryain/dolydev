#!/bin/bash

# EyeEngine Service Management Script
# 用于自动化编译、部署、重启、查看状态等维护操作

set -e

SERVICE_NAME="eyeengine-service"
SERVICE_FILE="eyeengine-service.service"
SERVICE_PATH="/etc/systemd/system/$SERVICE_FILE"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
MODULE_DIR="$(dirname "$SCRIPT_DIR")"
SERVICE_SCRIPT="$MODULE_DIR/zmq_service.py"
VENV_DIR="/home/pi/DOLY-DIY/venv"
PYTHON_BIN="$VENV_DIR/bin/python"
LOG_DIR="/home/pi/dolydev/logs"
LOG_FILE="$LOG_DIR/eyeengine.log"

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
    
    # 检查服务脚本
    if [ ! -f "$SERVICE_SCRIPT" ]; then
        print_error "服务脚本不存在: $SERVICE_SCRIPT"
        return 1
    fi
    
    # 检查 Python 环境
    if [ ! -x "$PYTHON_BIN" ]; then
        print_error "虚拟环境 Python 不存在: $PYTHON_BIN"
        return 1
    fi
    
    # 检查必要的 Python 模块
    if ! "$PYTHON_BIN" -c "import zmq" 2>/dev/null; then
        print_warning "虚拟环境中缺少 zmq 模块，尝试安装..."
        "$PYTHON_BIN" -m pip install pyzmq 2>/dev/null || print_error "无法安装 zmq"
    fi
    return 0
}

install_service() {
    print_status "安装 EyeEngine systemd 服务..."
    
    if ! check_dependencies; then 
        return 1
    fi
    
    # 复制服务文件
    sudo cp "$MODULE_DIR/scripts/$SERVICE_FILE" "$SERVICE_PATH"
    
    # 重新加载 systemd 配置
    sudo systemctl daemon-reload
    
    # 启用开机自启
    sudo systemctl enable "$SERVICE_NAME"
    
    print_success "EyeEngine 服务已安装"
    print_status "使用 'systemctl start $SERVICE_NAME' 启动服务"
}

uninstall_service() {
    print_status "卸载 EyeEngine 服务..."
    
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        sudo systemctl stop "$SERVICE_NAME"
    fi
    
    sudo systemctl disable "$SERVICE_NAME"
    
    if [ -f "$SERVICE_PATH" ]; then
        sudo rm "$SERVICE_PATH"
    fi
    
    sudo systemctl daemon-reload
    
    print_success "EyeEngine 服务已卸载"
}

start_service() {
    print_status "启动 EyeEngine 服务..."
    
    if ! check_dependencies; then 
        return 1
    fi
    
    sudo systemctl start "$SERVICE_NAME"
    sleep 3
    
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "EyeEngine 服务启动成功"
        show_status
        return 0
    else
        print_error "EyeEngine 服务启动失败"
        show_logs
        return 1
    fi
}

stop_service() {
    print_status "停止 EyeEngine 服务..."
    
    sudo systemctl stop "$SERVICE_NAME"
    sleep 1
    
    # 如果服务仍然处于激活状态，执行强制 kill
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_warning "服务停止超时，执行强制终止..."
        sudo systemctl kill -s KILL "$SERVICE_NAME"
        sleep 1
    fi
    
    print_success "EyeEngine 服务已停止"
}

restart_service() {
    print_status "重启 EyeEngine 服务..."
    
    if ! check_dependencies; then 
        return 1
    fi
    
    sudo systemctl restart "$SERVICE_NAME"
    sleep 3
    
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "EyeEngine 服务重启成功"
        show_status
        return 0
    else
        print_error "EyeEngine 服务重启失败"
        show_logs
        return 1
    fi
}

show_status() {
    print_status "EyeEngine 服务状态："
    echo ""
    sudo systemctl status "$SERVICE_NAME" --no-pager || true
    echo ""
}

show_logs() {
    print_status "EyeEngine 服务日志（最近 50 行）："
    echo ""
    sudo journalctl -u "$SERVICE_NAME" -n 50 --no-pager -q || true
    echo ""
}

watch_logs() {
    print_status "实时监看 EyeEngine 服务日志（按 Ctrl+C 退出）..."
    echo ""
    sudo journalctl -u "$SERVICE_NAME" -f --no-pager 2>/dev/null || true
}

redeploy_service() {
    print_status "重新部署 EyeEngine 服务..."
    
    # 停止现有服务
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        stop_service
    fi
    
    # 卸载旧服务
    if [ -f "$SERVICE_PATH" ]; then
        uninstall_service
    fi
    
    # 重新安装并启动
    install_service
    sleep 1
    start_service
    
    print_success "EyeEngine 服务已重新部署"
}

health_check() {
    print_status "检查 EyeEngine 服务健康状态..."
    
    if ! sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_error "EyeEngine 服务未运行"
        return 1
    fi
    
    # 检查服务 PID
    PID=$(sudo systemctl show -p MainPID --value "$SERVICE_NAME")
    if [ -z "$PID" ] || [ "$PID" == "0" ]; then
        print_error "无法获取服务 PID"
        return 1
    fi
    
    print_success "EyeEngine 服务正在运行 (PID: $PID)"
    
    # 检查内存使用
    MEMORY=$(ps aux | awk -v pid=$PID '$2 == pid {printf "%.1f%%", $4}')
    print_status "内存使用率: $MEMORY"
    
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
EyeEngine Service Management Script

用法: $0 <command>

命令:
  install      - 安装服务（需要 sudo）
  uninstall    - 卸载服务（需要 sudo）
  start        - 启动服务（需要 sudo）
  stop         - 停止服务（需要 sudo）
  restart      - 重启服务（需要 sudo）
  status       - 显示服务状态
  logs         - 显示最近的日志
  watch        - 实时监看日志
  redeploy     - 重新部署服务（需要 sudo）
  health       - 检查服务健康状态
  help         - 显示此帮助信息

示例:
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
