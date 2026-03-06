#!/usr/bin/env bash
# Serial Service 管理脚本
# 集成: build / install /redeploy / fix-unit / start / stop / restart / status / logs / monitor / uninstall
set -euo pipefail

SERVICE_NAME="doly-serial"
SERVICE_FILE="$SERVICE_NAME.service"
SERVICE_PATH="/etc/systemd/system/$SERVICE_FILE"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB_SERIAL_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT_DIR="$(cd "$LIB_SERIAL_DIR/../.." && pwd)"
BUILD_DIR="$LIB_SERIAL_DIR/build"
SERVICE_BINARY="$BUILD_DIR/serial_service"
TARGET_BIN="/usr/local/bin/serial_service"
INSTALL_CONFIG_DIR="/etc/doly"
INSTALL_CONFIG="$INSTALL_CONFIG_DIR/serial.yaml"
RUN_AS_USER=${RUN_AS_USER:-pi}
RUN_AS_GROUP=${RUN_AS_GROUP:-dialout}

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

check_build_deps() {
    if ! command -v cmake >/dev/null 2>&1; then
        print_error "cmake 未安装。请安装 cmake 或手动构建二进制。"
        return 1
    fi
    return 0
}

build_service() {
    print_status "构建 serial_service..."
    if [ -f "$LIB_SERIAL_DIR/build.sh" ]; then
        (cd "$LIB_SERIAL_DIR" && ./build.sh)
        return $?
    fi
    if ! check_build_deps; then return 1; fi
    mkdir -p "$BUILD_DIR"
    cmake -S "$LIB_SERIAL_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" -j$(nproc 2>/dev/null || echo 1)
    print_success "构建完成: $SERVICE_BINARY"
}

_install_unit_file() {
    # 将标准 unit 写入到目标路径，移除 PIDFile，确保 ExecStartPost chmod 两个 socket
    local exec_start="${TARGET_BIN} --config ${INSTALL_CONFIG}"
    print_status "写入 systemd unit: $SERVICE_PATH (User=$RUN_AS_USER Group=$RUN_AS_GROUP)"
    # expand exec_start variable but escape $f in script body
    sudo tee "$SERVICE_PATH" >/dev/null <<EOF
[Unit]
Description=Doly Serial Service
After=network.target

[Service]
Type=simple
ExecStart=${exec_start}
User=${RUN_AS_USER}
Group=${RUN_AS_GROUP}
# Ensure IPC socket files are world-writable so non-root clients can connect
ExecStartPost=/bin/sh -c 'sleep 0.1; for f in /tmp/doly_serial_pub.sock /tmp/doly_zmq.sock; do [ -e "\$f" ] && chmod 0666 "\$f" || true; done'
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
EOF
}

install_service() {
    print_status "安装 Serial 服务..."
    if [ ! -x "$SERVICE_BINARY" ]; then
        print_warning "未发现二进制 $SERVICE_BINARY，尝试构建"
        build_service || { print_error "构建失败，无法安装"; return 1; }
    fi

    sudo mkdir -p "$INSTALL_CONFIG_DIR"

    # 停止服务以便安全地替换二进制
    sudo systemctl stop "$SERVICE_NAME" || true

    print_status "复制二进制到 $TARGET_BIN"
    sudo cp "$SERVICE_BINARY" "$TARGET_BIN"
    sudo chmod +x "$TARGET_BIN"

    SRC_CONFIG="$REPO_ROOT_DIR/config/serial.yaml"
    if [ -f "$SRC_CONFIG" ]; then
        print_status "安装配置文件到 $INSTALL_CONFIG"
        sudo cp "$SRC_CONFIG" "$INSTALL_CONFIG"
    else
        print_warning "仓库内未找到 $SRC_CONFIG，跳过配置文件复制"
    fi

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
    print_status "卸载 Serial 服务..."
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
    print_status "启动 Serial 服务..."
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
    print_status "停止 Serial 服务..."
    sudo systemctl stop "$SERVICE_NAME"
    print_success "服务已停止"
}

restart_service() {
    print_status "重启 Serial 服务..."
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
    # 显示 unit 是否 enable
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
    print_status "尝试重载服务配置..."
    sudo systemctl reload "$SERVICE_NAME" 2>/dev/null || {
        print_warning "服务不支持 reload，自动重启..."
        restart_service
    }
}

_fix_unit() {
    print_status "修复 systemd unit: 移除 PIDFile 并修正 ExecStartPost"
    if [ -f "$SERVICE_PATH" ]; then
        sudo cp -a "$SERVICE_PATH" "${SERVICE_PATH}.bak" || true
    fi
    _install_unit_file
    sudo systemctl daemon-reload

    # 确保修复后的 unit 被 enable
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

# 部署命令: 如果已有二进制则跳过构建，否则先构建，再执行 redeploy 类似流程
# 与 redeploy 相比，deploy 不会在已有二进制时强制重新构建

deploy_service() {
    print_status "部署服务: 检查二进制并安装"

    # 构建二进制（如果尚未构建）
    if [ -x "$SERVICE_BINARY" ]; then
        print_status "检测到二进制 $SERVICE_BINARY，跳过构建"
    else
        print_status "未检测到二进制，开始构建"
        build_service || { print_error "构建失败，部署中止"; return 1; }
    fi

    # 如果尚未安装 unit，则直接调用 install_service
    if [ ! -f "$SERVICE_PATH" ]; then
        print_status "unit 文件不存在，调用 install_service 来完成安装"
        install_service || { print_error "install 失败，无法部署"; return 1; }
        return $?
    fi

    # 停止服务以便替换二进制
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_status "服务正在运行，停止以进行更新"
        sudo systemctl stop "$SERVICE_NAME"
        sleep 1
    fi

    # 解析 unit 链接的目标二进制（如果不同于默认）
    if [ -f "$SERVICE_PATH" ]; then
        local ES_LINE
        ES_LINE=$(grep -E "^ExecStart=" "$SERVICE_PATH" || true)
        if [ -n "$ES_LINE" ]; then
            local ES_VAL=${ES_LINE#ExecStart=}
            local PARSED_BIN
            PARSED_BIN=$(echo "$ES_VAL" | awk '{print $1}')
            if [ -n "$PARSED_BIN" ]; then
                TARGET_BIN="$PARSED_BIN"
                print_status "unit 中 ExecStart 指向: $TARGET_BIN"
            fi
        fi
    fi

    # 替换二进制
    print_status "复制 $SERVICE_BINARY 到 $TARGET_BIN"
    sudo cp "$SERVICE_BINARY" "$TARGET_BIN"
    sudo chmod +x "$TARGET_BIN"

    # 同步配置文件
    SRC_CONFIG="$REPO_ROOT_DIR/config/serial.yaml"
    if [ -f "$SRC_CONFIG" ]; then
        print_status "复制配置文件到 $INSTALL_CONFIG"
        sudo mkdir -p "$INSTALL_CONFIG_DIR"
        sudo cp "$SRC_CONFIG" "$INSTALL_CONFIG"
    else
        print_warning "仓库内未找到 $SRC_CONFIG，跳过配置文件复制"
    fi

    sudo systemctl daemon-reload

    # 确保 unit 已 enable
    if ! sudo systemctl is-enabled --quiet "$SERVICE_NAME"; then
        print_status "unit 未 enable，正在 enable"
        sudo systemctl enable "$SERVICE_NAME" || print_warning "enable 失败"
    fi

    sudo systemctl restart "$SERVICE_NAME"
    sleep 1
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "部署成功，服务已运行"
        show_status
    else
        print_error "部署失败: 服务未运行"
        show_logs
        return 1
    fi
}

redeploy_service() {
    print_status "重新部署服务: 可选构建并替换二进制"
    # 如果 unit 文件不存在，说明服务尚未安装，直接调用 install
    if [ ! -f "$SERVICE_PATH" ]; then
        print_warning "未检测到 unit 文件，正在执行 install 以创建 unit"
        install_service || { print_error "install 失败，不能重部署"; return 1; }
        # install_service 会完成启停，所以直接返回
        return $?
    fi

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

    # 同步配置文件：每次部署/重新部署都要把仓库中的最新 config/serial.yaml 覆盖到 /etc/doly/serial.yaml
    SRC_CONFIG="$REPO_ROOT_DIR/config/serial.yaml"
    if [ -f "$SRC_CONFIG" ]; then
        print_status "复制配置文件到 $INSTALL_CONFIG"
        sudo mkdir -p "$INSTALL_CONFIG_DIR"
        sudo cp "$SRC_CONFIG" "$INSTALL_CONFIG"
    else
        print_warning "仓库内未找到 $SRC_CONFIG，跳过配置文件复制"
    fi

    # 重新加载 systemd 配置
    sudo systemctl daemon-reload

    # 确保 unit 被 enable（以便在系统重启后能自动启动）
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
    echo "Serial 服务管理脚本"
    echo
    echo "用法: $0 <命令>"
    echo
    echo "命令:"
    echo "  build       构建二进制 (cmake)"
    echo "  install     安装并启用 systemd 服务 (复制二进制、配置并写 unit)"
    echo "  uninstall   卸载服务并删除 unit" 
    echo "  start       启动服务"
    echo "  stop        停止服务"
    echo "  restart     重启服务"
    echo "  status      查看服务状态和最近日志"
    echo "  logs        实时查看服务日志 (跟随)"
    echo "  monitor     实时监控服务日志 (同 logs)"
    echo "  reload      重载配置 (不支持则重启)"
    echo "  deploy      检查并部署服务，已有二进制时跳过构建，否则构建后安装/更新"
    echo "  redeploy    如果尚未安装会执行 install，否则构建(可选)并替换二进制然后重启服务"
    echo "  fix-unit    修复 systemd unit（移除 PIDFile，修正 ExecStartPost）并重启服务"
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
    deploy)
        deploy_service
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
