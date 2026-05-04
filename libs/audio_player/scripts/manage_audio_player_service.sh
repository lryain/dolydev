#!/usr/bin/env bash
# Audio Player Service 管理脚本
# 功能：构建 / 安装 / redeploy / fix-unit / start / stop / restart / status / logs / monitor
# 风格参照 libs/drive/scripts/manage_service.sh

set -euo pipefail

SERVICE_NAME="doly-audio-player"
SERVICE_FILE="$SERVICE_NAME.service"
SERVICE_PATH="/etc/systemd/system/$SERVICE_FILE"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIB_AUDIO_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
REPO_ROOT_DIR="$(cd "$LIB_AUDIO_DIR/../.." && pwd)"
BUILD_DIR="$LIB_AUDIO_DIR/build"
INSTALL_DIR="$LIB_AUDIO_DIR/install"
SERVICE_BINARY="$INSTALL_DIR/audio_player_service"
TARGET_BIN="/usr/local/bin/audio_player_service"
AUDIO_SOURCE_DIR="$LIB_AUDIO_DIR/audios"
AUDIO_INSTALL_DIR="/.doly/sounds"
# 使用仓库内的配置文件（不再复制到 /etc/doly）
INSTALL_CONFIG_DIR="$REPO_ROOT_DIR/config"
INSTALL_CONFIG="$INSTALL_CONFIG_DIR/audio_player.yaml"
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
    print_status "检查依赖与二进制..."
    if [ ! -f "$SERVICE_BINARY" ]; then
        print_warning "二进制未找到: $SERVICE_BINARY"
        print_status "请先运行 build 或在 redeploy 时允许构建"
        return 1
    fi
    print_success "依赖检查通过"
    return 0
}

copy_audio_files() {
    if [ ! -d "$AUDIO_SOURCE_DIR" ]; then
        print_warning "音频源目录不存在: $AUDIO_SOURCE_DIR，跳过音频部署"
        return 0
    fi

    print_status "同步音频文件到系统目录: $AUDIO_INSTALL_DIR"
    sudo mkdir -p "$AUDIO_INSTALL_DIR"

    local src_file dest_file rel_path
    while IFS= read -r -d '' src_file; do
        rel_path="${src_file#$AUDIO_SOURCE_DIR/}"
        dest_file="$AUDIO_INSTALL_DIR/$rel_path"
        if [ -f "$dest_file" ]; then
            print_status "已存在音频文件，跳过: $rel_path"
            continue
        fi
        print_status "复制音频文件: $rel_path"
        sudo mkdir -p "$(dirname "$dest_file")"
        sudo cp "$src_file" "$dest_file"
    done < <(find "$AUDIO_SOURCE_DIR" -type f -print0)
}

build_service() {
    print_status "构建 audio_player..."
    if [ -f "$LIB_AUDIO_DIR/build.sh" ]; then
        (cd "$LIB_AUDIO_DIR" && ./build.sh)
        return $?
    fi
    print_status "使用 cmake 构建"
    mkdir -p "$BUILD_DIR"
    cmake -S "$LIB_AUDIO_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release
    cmake --build "$BUILD_DIR" -j$(nproc 2>/dev/null || echo 1)
    # 将编译产物复制到 install 目录，便于提交仓库并用于部署
    mkdir -p "$INSTALL_DIR"
    if [ -f "$BUILD_DIR/audio_player_service" ]; then
        cp "$BUILD_DIR/audio_player_service" "$SERVICE_BINARY"
        chmod +x "$SERVICE_BINARY"
        print_success "构建并复制完成: $SERVICE_BINARY"
    else
        print_warning "构建完成，但未找到 $BUILD_DIR/audio_player_service，无法复制到 $INSTALL_DIR"
    fi
}

_install_unit_file() {
    local exec_start="${TARGET_BIN} ${INSTALL_CONFIG}"
    print_status "写入 systemd unit: $SERVICE_PATH (User=$RUN_AS_USER Group=$RUN_AS_GROUP)"
    local local_service="$SCRIPT_DIR/$SERVICE_FILE"
    if [ -f "$local_service" ]; then
        print_status "检测到本地 unit 文件 $local_service，复制到 $SERVICE_PATH"
        sudo cp "$local_service" "$SERVICE_PATH"
        # 确保 ExecStart/ User/Group 使用当前脚本配置
        sudo sed -i "s|^ExecStart=.*|ExecStart=${exec_start}|" "$SERVICE_PATH" || true
        if grep -q "^User=" "$SERVICE_PATH"; then
            sudo sed -i "s|^User=.*|User=${RUN_AS_USER}|" "$SERVICE_PATH" || true
        else
            sudo sed -i "/^ExecStart=.*/a User=${RUN_AS_USER}" "$SERVICE_PATH" || true
        fi
        if grep -q "^Group=" "$SERVICE_PATH"; then
            sudo sed -i "s|^Group=.*|Group=${RUN_AS_GROUP}|" "$SERVICE_PATH" || true
        else
            sudo sed -i "/^ExecStart=.*/a Group=${RUN_AS_GROUP}" "$SERVICE_PATH" || true
        fi
        return 0
    fi
    # fallback: 写入标准 unit
    sudo tee "$SERVICE_PATH" >/dev/null <<EOF
[Unit]
Description=Doly Audio Player Service
After=network.target

[Service]
Type=simple
ExecStart=${exec_start}
User=${RUN_AS_USER}
Group=${RUN_AS_GROUP}
# Ensure IPC socket files are world-writable so non-root clients can connect
ExecStartPost=/bin/sh -c 'sleep 0.1; for f in /tmp/doly_audio_player_cmd.sock /tmp/doly_audio_player_status.sock /tmp/doly_audio_player_stream.sock; do [ -e "\$f" ] && chmod 0666 "\$f" || true; done'
Restart=on-failure
RestartSec=2

[Install]
WantedBy=multi-user.target
EOF
}

install_service() {
    print_status "安装 Audio Player 服务..."
    if [ ! -x "$SERVICE_BINARY" ]; then
        print_warning "未发现二进制 $SERVICE_BINARY，尝试构建"
        build_service || { print_error "构建失败，无法安装"; return 1; }
    fi

    sudo mkdir -p "$INSTALL_CONFIG_DIR"

    # 停止服务以便替换二进制
    sudo systemctl stop "$SERVICE_NAME" || true

    print_status "复制二进制到 $TARGET_BIN"
    sudo cp "$SERVICE_BINARY" "$TARGET_BIN"
    sudo chmod +x "$TARGET_BIN"

    SRC_CONFIG="$REPO_ROOT_DIR/config/audio_player.yaml"
    if [ -f "$SRC_CONFIG" ]; then
        if [ "$SRC_CONFIG" != "$INSTALL_CONFIG" ]; then
            print_status "安装配置文件到 $INSTALL_CONFIG"
            sudo mkdir -p "$(dirname "$INSTALL_CONFIG")"
            sudo cp "$SRC_CONFIG" "$INSTALL_CONFIG"
        else
            print_status "配置文件位于仓库，将直接使用: $INSTALL_CONFIG"
        fi
    else
        print_warning "仓库内未找到 $SRC_CONFIG，跳过配置文件复制"
    fi

    _install_unit_file
    copy_audio_files

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
    print_status "卸载 Audio Player 服务..."
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
    print_status "启动 Audio Player 服务..."
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
    print_status "停止 Audio Player 服务..."
    sudo systemctl stop "$SERVICE_NAME"
    print_success "服务已停止"
}

restart_service() {
    print_status "重启 Audio Player 服务..."
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

# 部署命令: 仅在不存在二进制时构建，其余流程与 redeploy 相同
# redeploy 会强制构建新版本。
deploy_service() {
    print_status "部署服务: 检查二进制并更新"

    if [ -x "$SERVICE_BINARY" ]; then
        print_status "检测到二进制 $SERVICE_BINARY，跳过构建"
    else
        print_status "未检测到二进制，开始构建"
        build_service || { print_error "构建失败，部署中止"; return 1; }
    fi

    # 下面逻辑与 redeploy 相同，用以复制、启用并重启服务
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_status "服务运行中，先停止以便更新"
        sudo systemctl stop "$SERVICE_NAME"
        sleep 1
    fi

    # 如果 unit 不存在，先写入
    if [ ! -f "$SERVICE_PATH" ]; then
        _install_unit_file
    fi

    print_status "复制 $SERVICE_BINARY 到 $TARGET_BIN"
    sudo cp "$SERVICE_BINARY" "$TARGET_BIN"
    sudo chmod +x "$TARGET_BIN"

    SRC_CONFIG="$REPO_ROOT_DIR/config/audio_player.yaml"
    if [ -f "$SRC_CONFIG" ]; then
        if [ "$SRC_CONFIG" != "$INSTALL_CONFIG" ]; then
            print_status "复制配置文件到 $INSTALL_CONFIG"
            sudo mkdir -p "$(dirname "$INSTALL_CONFIG")"
            sudo cp "$SRC_CONFIG" "$INSTALL_CONFIG"
        else
            print_status "配置文件位于仓库，将直接使用: $INSTALL_CONFIG"
        fi
    else
        print_warning "仓库内未找到 $SRC_CONFIG，跳过配置文件复制"
    fi

    copy_audio_files

    sudo systemctl daemon-reload
    sudo systemctl enable "$SERVICE_NAME" || true
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
    print_status "重新部署服务: 强制构建并替换二进制"
    # 必须重新编译，即使已有二进制
    build_service || { print_error "构建失败，无法重部署"; return 1; }

    # 剩余逻辑与原来相同
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

    # 同步配置文件：每次部署/重新部署确保使用仓库中的 config/audio_player.yaml（目标: $INSTALL_CONFIG）
    SRC_CONFIG="$REPO_ROOT_DIR/config/audio_player.yaml"
    if [ -f "$SRC_CONFIG" ]; then
        if [ "$SRC_CONFIG" != "$INSTALL_CONFIG" ]; then
            print_status "复制配置文件到 $INSTALL_CONFIG"
            sudo mkdir -p "$(dirname "$INSTALL_CONFIG")"
            sudo cp "$SRC_CONFIG" "$INSTALL_CONFIG"
        else
            print_status "配置文件位于仓库，将直接使用: $INSTALL_CONFIG"
        fi
    else
        print_warning "仓库内未找到 $SRC_CONFIG，跳过配置文件复制"
    fi

    # 如果 systemd unit 文件不存在则写入（避免 enable 时报错）
    if [ ! -f "$SERVICE_PATH" ]; then
        print_status "系统单元 $SERVICE_PATH 不存在，正在写入 unit 文件"
        _install_unit_file
    fi

    copy_audio_files

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
    echo "Audio Player 服务管理脚本"
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
    echo "  deploy      若已有二进制则跳过构建，否则构建后安装/更新"
    echo "  redeploy    构建(始终)并替换二进制然后重启服务"
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
