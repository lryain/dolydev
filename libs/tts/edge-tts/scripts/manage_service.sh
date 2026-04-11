#!/bin/bash

set -euo pipefail

SERVICE_NAME="edge-tts"
SERVICE_FILE="edge-tts.service"
SERVICE_PATH="/etc/systemd/system/$SERVICE_FILE"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/.."
# Use the shared DOLY python virtualenv by default
VENV_DIR="/home/pi/dolydev/.venv"
if [ -d "$VENV_DIR" ]; then
    # shellcheck disable=SC1090
    source "$VENV_DIR/bin/activate"
fi
PYTHON_BIN="${PYTHON_BIN:-python3}"
REQUIREMENTS="$SCRIPT_DIR/requirements.txt"
SERVICE_SCRIPT="$SCRIPT_DIR/zmq_tts_service.py"
LOG_DIR="/tmp/logs/edge-tts"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() { echo -e "${BLUE}[info]${NC} $1"; }
print_success() { echo -e "${GREEN}[ok]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[warn]${NC} $1"; }
print_error() { echo -e "${RED}[err]${NC} $1"; }

ensure_logdir() {
    if [ ! -d "$LOG_DIR" ]; then
        print_status "创建日志目录 $LOG_DIR"
        mkdir -p "$LOG_DIR"
    fi
}

check_dependencies() {
    print_status "检查依赖"
    if ! command -v "$PYTHON_BIN" >/dev/null 2>&1; then
        print_error "$PYTHON_BIN 未找到，请设置 PYTHON_BIN 环境变量或安装 python3"
        return 1
    fi
    if ! command -v ffmpeg >/dev/null 2>&1; then
        print_warning "ffmpeg 未安装，语音输出 WAV 需要 ffmpeg。请使用 'sudo apt install ffmpeg' 安装。"
    fi
    if [ ! -f "$SERVICE_SCRIPT" ]; then
        print_error "服务脚本缺失: $SERVICE_SCRIPT"
        return 1
    fi
    ensure_logdir
    print_success "依赖就绪"
}

prepare_service() {
    print_status "安装 Python 依赖"
    if [ ! -f "$REQUIREMENTS" ]; then
        print_warning "$REQUIREMENTS 不存在，跳过"
        return
    fi
    "$PYTHON_BIN" -m pip install -r "$REQUIREMENTS"
}

install_service() {
    print_status "安装 edge-tts systemd 服务"
    check_dependencies
    prepare_service
    sudo cp "$SCRIPT_DIR/scripts/$SERVICE_FILE" "$SERVICE_PATH"
    sudo systemctl daemon-reload
    sudo systemctl enable "$SERVICE_NAME"
    print_success "服务安装并启用"
}

uninstall_service() {
    print_status "卸载 edge-tts 服务"
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        sudo systemctl stop "$SERVICE_NAME"
    fi
    sudo systemctl disable "$SERVICE_NAME"
    [ -f "$SERVICE_PATH" ] && sudo rm "$SERVICE_PATH"
    sudo systemctl daemon-reload
    print_success "服务已卸载"
}

start_service() {
    print_status "启动 edge-tts 服务"
    check_dependencies
    sudo systemctl start "$SERVICE_NAME"
    sleep 1
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "服务已启动"
    else
        print_error "启动失败"
        show_logs
        exit 1
    fi
}

stop_service() {
    print_status "停止 edge-tts 服务"
    sudo systemctl stop "$SERVICE_NAME"
    print_success "服务已停止"
}

restart_service() {
    print_status "重启 edge-tts 服务"
    sudo systemctl restart "$SERVICE_NAME"
    sleep 1
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "服务重启成功"
    else
        print_error "重启失败"
        exit 1
    fi
}

show_status() {
    sudo systemctl status "$SERVICE_NAME" --no-pager -l
}

show_logs() {
    sudo journalctl -u "$SERVICE_NAME" --no-pager -n 20
}

monitor_service() {
    echo "按 Ctrl+C 退出"
    sudo journalctl -u "$SERVICE_NAME" -f
}

reload_service() {
    print_status "重载 edge-tts 服务"
    sudo systemctl reload "$SERVICE_NAME" || {
        print_warning "服务不支持 reload，改用 restart"
        restart_service
    }
}

redeploy_service() {
    print_status "重新部署 edge-tts 服务"
    prepare_service
    if [ -f "$SERVICE_PATH" ]; then
        stop_service
    fi
    install_service
    start_service
}

show_help() {
    cat <<EOF
Edge TTS 服务管理脚本
用法: $0 <command>
命令:
  prepare     安装 python 依赖
  install     安装 systemd 服务并启用
  uninstall   卸载 service
  start       启动服务
  stop        停止服务
  restart     重启服务
  status      显示 systemd 状态
  logs        显示最近日志
  monitor     实时跟踪日志
  reload      重载或重启服务
  redeploy    重新安装/启动
  help        显示此帮助
EOF
}

case "${1:-help}" in
    prepare)
        prepare_service
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
        reload_service
        ;;
    redeploy)
        redeploy_service
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        print_error "未知命令 $1"
        show_help
        exit 1
        ;;
esac
