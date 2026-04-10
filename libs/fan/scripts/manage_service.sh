#!/bin/bash
# Fan Control Service Management Script (merged: build/run/deploy)
set -e

SERVICE_NAME="fan-control"
SERVICE_FILE="fan-control.service"
SERVICE_PATH="/etc/systemd/system/$SERVICE_FILE"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$SCRIPT_DIR/.."
BUILD_DIR="$PROJECT_ROOT/build"
SERVICE_BINARY="$BUILD_DIR/fan_service"
SERVICE_FILE_PATH="$SCRIPT_DIR/$SERVICE_FILE"
CONFIG_FILE="/home/pi/dolydev/config/fan_config.yaml"
LOG_DIR="/home/pi/dolydev/data/logs"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m'

print_status() { echo -e "${BLUE}[INFO]${NC} $1"; }
print_success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
print_warning() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
print_error() { echo -e "${RED}[ERROR]${NC} $1"; }

build_service() {
    print_status "Building service..."
    mkdir -p "$BUILD_DIR"
    pushd "$BUILD_DIR" >/dev/null
    cmake ..
    make -j$(nproc)
    popd >/dev/null
    print_success "Build completed"
}

redeploy_service() {
    print_status "Redeploying service..."
    build_service || exit 1

    # Update systemd service file
    sudo cp "$SERVICE_FILE_PATH" "$SERVICE_PATH"
    sudo systemctl daemon-reload

    # Restart
    print_status "Restarting $SERVICE_NAME"
    sudo systemctl restart "$SERVICE_NAME"

    sleep 2
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "Redeploy succeeded"
        show_status
    else
        print_error "Redeploy failed"
        sudo journalctl -u "$SERVICE_NAME" -n 20 --no-pager
        exit 1
    fi
}

deploy_service() {
    print_status "Deploying service (skip build)..."
    if [ ! -f "$SERVICE_BINARY" ]; then
        print_error "Binary not found: $SERVICE_BINARY"
        exit 1
    fi

    TARGET_BIN="/usr/local/bin/$(basename "$SERVICE_BINARY")"
    if [ -f "$SERVICE_FILE_PATH" ]; then
        ES_LINE=$(grep -E "^ExecStart=" "$SERVICE_FILE_PATH" || true)
        if [ -n "$ES_LINE" ]; then
            ES_VAL=${ES_LINE#ExecStart=}
            ES_FIRST=$(echo "$ES_VAL" | awk '{print $1}')
            SB_REAL=$(readlink -f "$SERVICE_BINARY" || echo "$SERVICE_BINARY")
            EF_REAL=$(readlink -f "$ES_FIRST" || echo "$ES_FIRST")
            if [ "$EF_REAL" != "$SB_REAL" ]; then
                TARGET_BIN="$ES_FIRST"
            fi
        fi
    fi

    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_status "Service running, stopping before replacing binary"
        sudo systemctl stop "$SERVICE_NAME"
        sleep 1
    fi

    sudo cp "$SERVICE_FILE_PATH" "$SERVICE_PATH"
    sudo systemctl daemon-reload

    print_status "Copying $SERVICE_BINARY to $TARGET_BIN"
    sudo cp "$SERVICE_BINARY" "$TARGET_BIN"
    sudo chmod +x "$TARGET_BIN"

    print_status "Restarting $SERVICE_NAME"
    sudo systemctl restart "$SERVICE_NAME"
    sleep 2
    if sudo systemctl is-active --quiet "$SERVICE_NAME"; then
        print_success "Deploy succeeded, service running"
        show_status
    else
        print_error "Deploy failed: service not running"
        sudo journalctl -u "$SERVICE_NAME" -n 20 --no-pager
        exit 1
    fi
}

run_service_console() {
    echo "Starting Fan Service in foreground (console mode)..."
    # clear old socket
    rm -f /tmp/doly_fan_zmq.sock 2>/dev/null || true
    export LD_LIBRARY_PATH=/home/pi/DOLY-DIY/SDK/lib:/usr/local/lib:/usr/lib/aarch64-linux-gnu:$LD_LIBRARY_PATH
    echo "Configuration: $CONFIG_FILE"
    "$SERVICE_BINARY" --console --bus-endpoint ipc:///tmp/doly_fan_zmq.sock -c "$CONFIG_FILE"
}

show_status() {
    echo
    echo "=== Service Status ==="
    sudo systemctl status "$SERVICE_NAME" --no-pager -l || true
    echo
    echo "=== Recent Logs ==="
    sudo journalctl -u "$SERVICE_NAME" -n 10 --no-pager
}

case "${1:-help}" in
    build) build_service ;;
    redeploy) redeploy_service ;;
    deploy) deploy_service ;;
    run) run_service_console ;;
    status) show_status ;;
    start) sudo systemctl start "$SERVICE_NAME"; show_status ;;
    stop) sudo systemctl stop "$SERVICE_NAME"; print_status "Stopped" ;;
    restart) sudo systemctl restart "$SERVICE_NAME"; show_status ;;
    logs) sudo journalctl -u "$SERVICE_NAME" -f ;;
    uninstall)
        sudo systemctl stop "$SERVICE_NAME" || true
        sudo systemctl disable "$SERVICE_NAME" || true
        sudo rm "$SERVICE_PATH" || true
        sudo systemctl daemon-reload
        print_status "Uninstalled"
        ;;
    install)
        sudo cp "$SERVICE_FILE_PATH" "$SERVICE_PATH"
        sudo systemctl daemon-reload
        sudo systemctl enable "$SERVICE_NAME"
        print_status "Installed"
        ;;
    help|--help|-h|*)
        echo "Usage: $0 {build|redeploy|deploy|run|status|start|stop|restart|logs|install|uninstall}"
        ;;
esac
