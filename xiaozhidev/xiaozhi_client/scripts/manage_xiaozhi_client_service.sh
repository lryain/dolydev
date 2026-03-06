#!/usr/bin/env bash
# 小智 (xiaozhi_client) 服务管理脚本
# 参考：libs/drive/scripts/manage_service.sh & libs/audio_player/scripts/manage_audio_player_service.sh
# 功能：构建 / 安装 / redeploy / fix-unit / start / stop / restart / status / logs / monitor

set -euo pipefail

SERVICE_NAME="xiaozhi_client"
SERVICE_FILE="$SERVICE_NAME.service"
SERVICE_PATH="/etc/systemd/system/$SERVICE_FILE"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT_DIR="$(cd "$LIB_DIR/../.." && pwd)"
BUILD_DIR="$LIB_DIR/build"
SERVICE_BINARY="$BUILD_DIR/xiaozhi_client"
TARGET_BIN="/usr/local/bin/xiaozhi_client"
# 默认以 wakeup 模式启动（满足需求“默认带上wakeup参数”）
RUN_MODE=${RUN_MODE:-wakeup}
RUN_AS_USER=${RUN_AS_USER:-pi}
RUN_AS_GROUP=${RUN_AS_GROUP:-audio}

# 颜色输出
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
    print_status "检查二进制: $SERVICE_BINARY"
    if [ ! -x "$SERVICE_BINARY" ]; then
        print_warning "未找到可执行二进制: $SERVICE_BINARY"
        print_status "请先运行 build 或在 redeploy 时允许构建"
        return 1
    fi
    print_success "依赖检查通过"
    return 0
}

build_service() {
    print_status "构建 xiaozhi_client..."
    if [ -f "$LIB_DIR/build.sh" ]; then
        (cd "$LIB_DIR" && ./build.sh)
        return $?
    fi
    print_status "使用 cmake 构建"
    mkdir -p "$BUILD_DIR"
    cmake -S "$LIB_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" -j$(nproc 2>/dev/null || echo 1)
    print_success "构建完成: $SERVICE_BINARY"
}

_install_unit_file() {
    local exec_start="${TARGET_BIN} ${RUN_MODE}"
    print_status "写入 systemd unit: $SERVICE_PATH (User=$RUN_AS_USER Group=$RUN_AS_GROUP)"
    sudo tee "$SERVICE_PATH" >/dev/null <<EOF
[Unit]
Description=Xiaozhi Client Service
After=network.target

[Service]
Type=simple
ExecStart=${exec_start}
User=${RUN_AS_USER}
Group=${RUN_AS_GROUP}
# 在启动后修复 socket 权限，便于其它非 root 模块访问
ExecStartPost=/bin/sh -c 'sleep 0.1; for f in /tmp/doly_xiaozhi*.sock; do [ -e "\$f" ] && chmod 0666 "\$f" || true; done'
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
EOF
}

install_service() {
    print_status "安装 xiaozhi_client 服务..."
    if [ ! -x "$SERVICE_BINARY" ]; then
        print_warning "未发现二进制 $SERVICE_BINARY，尝试构建"
        build_service || { print_error "构建失败，无法安装"; return 1; }
    fi

    # 停止服务以便替换二进制
    sudo systemctl stop "$SERVICE_NAME" || true

    print_status "复制二进制到 $TARGET_BIN"
    sudo cp "$SERVICE_BINARY" "$TARGET_BIN"
    sudo chmod +x "$TARGET_BIN"

    _install_unit_file

    sudo systemctl daemon-reload
    sudo systemctl enable "$SERVICE_NAME"
    sudo systemctl restart "$SERVICE_NAME"
    sleep 1
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "服务已安装并运行"
        show_status
    else
        print_error "服务未能启动，查看日志获取更多信息"
        show_logs
        return 1
    fi
}

uninstall_service() {
    print_status "卸载 xiaozhi_client 服务..."
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        sudo systemctl stop "$SERVICE_NAME"
    fi
    sudo systemctl disable "$SERVICE_NAME" || true
    if [ -f "$SERVICE_PATH" ]; then
        print_status "备份并删除 unit: ${SERVICE_PATH}.bak"
        sudo cp -a "$SERVICE_PATH" "${SERVICE_PATH}.bak" || true
        sudo rm -f "$SERVICE_PATH"
        sudo systemctl daemon-reload
    fi
    print_success "服务已卸载"
}

start_service() {
    print_status "启动 xiaozhi_client 服务..."
    sudo systemctl start "$SERVICE_NAME"
    sleep 1
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
    print_status "停止 xiaozhi_client 服务..."
    sudo systemctl stop "$SERVICE_NAME"
    print_success "服务已停止"
}

restart_service() {
    print_status "重启 xiaozhi_client 服务..."
    sudo systemctl restart "$SERVICE_NAME"
    sleep 1
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "服务重启成功"
    else
        print_error "服务重启失败"
        show_logs
        return 1
    fi
}

show_status() {
    echo
    echo "=== 服务状态 ==="
    sudo systemctl status "$SERVICE_NAME" --no-pager -l
    echo
    if sudo systemctl is-enabled --quiet "$SERVICE_NAME" 2>/dev/null; then
        echo "Service is enabled (will start at boot)"
    else
        echo "Service is disabled (will NOT start at boot)"
    fi
    echo
    echo "=== 最近日志 ==="
    sudo journalctl -u "$SERVICE_NAME" -n 10 --no-pager
}

show_logs() {
    echo "=== 服务日志 ==="
    sudo journalctl -u "$SERVICE_NAME" --no-pager -f
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

_fix_unit() {
    print_status "修复 systemd unit: 写入标准 unit 并修复 socket 权限"
    if [ -f "$SERVICE_PATH" ]; then
        sudo cp -a "$SERVICE_PATH" "${SERVICE_PATH}.bak" || true
    fi
    _install_unit_file
    sudo systemctl daemon-reload

    if ! sudo systemctl is-enabled --quiet "$SERVICE_NAME"; then
        print_status "unit 未 enable，正在 enable"
        sudo systemctl enable "$SERVICE_NAME" || print_warning "enable 失败"
    fi

    sudo systemctl restart "$SERVICE_NAME"
    sleep 1
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "unit 修复并重启成功"
        show_status
    else
        print_error "修复后服务未能启动，查看日志以排查问题"
        show_logs
        return 1
    fi
}

redeploy_service() {
    print_status "重新部署服务: 可选构建并替换二进制"
    if [ -x "$SERVICE_BINARY" ]; then
        print_status "检测到已构建的二进制 $SERVICE_BINARY"
    else
        print_warning "未检测到二进制，尝试构建"
        build_service || { print_error "构建失败，无法重部署"; return 1; }
    fi

    # 从 unit 中解析 ExecStart 指定的二进制路径（若存在）
    if [ -f "$SERVICE_PATH" ]; then
        local ES_LINE
        ES_LINE=$(grep -E "^ExecStart=" "$SERVICE_PATH" || true)
        if [ -n "$ES_LINE" ]; then
            local ES_VAL=${ES_LINE#ExecStart=}
            local PARSED_BIN
            PARSED_BIN=$(echo "$ES_VAL" | awk '{print $1}')
            if [ -n "$PARSED_BIN" ]; then
                TARGET_BIN="$PARSED_BIN"
                print_status "检测到 unit 中的 ExecStart，目标二进制: $TARGET_BIN"
            fi
        fi
    fi

    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_status "停止服务以便替换二进制"
        sudo systemctl stop "$SERVICE_NAME"
        sleep 1
    fi

    print_status "复制 $SERVICE_BINARY 到 $TARGET_BIN"
    sudo cp "$SERVICE_BINARY" "$TARGET_BIN"
    sudo chmod +x "$TARGET_BIN"

    # 如果 systemd unit 文件不存在则写入（避免 enable 时报错）
    if [ ! -f "$SERVICE_PATH" ]; then
        print_status "系统单元 $SERVICE_PATH 不存在，正在写入 unit 文件"
        _install_unit_file
    fi

    sudo systemctl daemon-reload

    if ! sudo systemctl is-enabled --quiet "$SERVICE_NAME"; then
        print_status "检测到服务未被 enable，正在 enable"
        if sudo systemctl enable "$SERVICE_NAME"; then
            print_success "服务已 enable，会在启动时自动运行"
        else
            print_warning "无法 enable 服务，请检查权限或 unit 文件"
        fi
    fi

    sudo systemctl restart "$SERVICE_NAME"
    sleep 1
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "重新部署成功，服务已运行"
        show_status
    else
        print_error "重新部署失败: 服务未运行"
        show_logs
        return 1
    fi
}

show_help() {
    echo "Xiaozhi Client 服务管理脚本"
    echo
    echo "用法: $0 <命令>"
    echo
    echo "命令:"
    echo "  build       构建二进制 (cmake)"
    echo "  install     安装并启用 systemd 服务 (复制二进制 并写 unit，默认参数: ${RUN_MODE})"
    echo "  uninstall   卸载服务并删除 unit"
    echo "  start       启动服务"
    echo "  stop        停止服务"
    echo "  restart     重启服务"
    echo "  status      查看服务状态和最近日志"
    echo "  logs        实时查看服务日志 (跟随)"
    echo "  monitor     实时监控服务日志 (同 logs)"
    echo "  reload      重载配置 (不支持则重启)"
    echo "  redeploy    构建(可选)并替换二进制然后重启服务"
    echo "  fix-unit    修复/写入标准 unit（确保 socket 权限）并重启服务"
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
    logs|monitor)
        show_logs
        ;;
    reload)
        reload_config
        ;;
    redeploy)
        redeploy_service
        ;;
    fix-unit|fix)
        _fix_unit
        ;;
    help|--help|-h)
        show_help
        ;;
    *)
        print_error "未知命令: $1"
        echo
        show_help
        exit 1
        ;;
esac
