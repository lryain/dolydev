#!/bin/bash
# Widget Service 管理脚本

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="${SCRIPT_DIR}/build"
SERVICE_BIN="${BUILD_DIR}/widget_service"
CONFIG_FILE="${SCRIPT_DIR}/config/widgets.default.json"
LOG_FILE="${SCRIPT_DIR}/widget_service.log"
PID_FILE="/tmp/doly_widget_service.pid"

case "$1" in
    start)
        echo "[Widget Service] 启动..."
        # 先杀死之前的进程
        if [ -f "$PID_FILE" ]; then
            kill $(cat "$PID_FILE") 2>/dev/null
            rm -f "$PID_FILE"
            sleep 1
        fi
        pkill -f "widget_service" 2>/dev/null
        sleep 1
        
        if [ ! -f "$SERVICE_BIN" ]; then
            echo "[Widget Service] 可执行文件不存在，需要先编译"
            echo "  cd ${BUILD_DIR} && cmake .. && make -j2"
            exit 1
        fi
        
        $SERVICE_BIN --config "$CONFIG_FILE" > "$LOG_FILE" 2>&1 &
        echo $! > "$PID_FILE"
        echo "[Widget Service] 已启动 PID=$(cat $PID_FILE)"
        echo "[Widget Service] 日志: $LOG_FILE"
        ;;
    stop)
        echo "[Widget Service] 停止..."
        if [ -f "$PID_FILE" ]; then
            kill $(cat "$PID_FILE") 2>/dev/null
            rm -f "$PID_FILE"
        fi
        pkill -f "widget_service" 2>/dev/null
        echo "[Widget Service] 已停止"
        ;;
    restart)
        $0 stop
        sleep 1
        $0 start
        ;;
    status)
        if [ -f "$PID_FILE" ] && kill -0 $(cat "$PID_FILE") 2>/dev/null; then
            echo "[Widget Service] 运行中 PID=$(cat $PID_FILE)"
        else
            echo "[Widget Service] 未运行"
        fi
        ;;
    log)
        tail -f "$LOG_FILE"
        ;;
    build)
        echo "[Widget Service] 编译..."
        mkdir -p "$BUILD_DIR"
        cd "$BUILD_DIR" && cmake .. && make -j2
        echo "[Widget Service] 编译完成"
        ;;
    *)
        echo "用法: $0 {start|stop|restart|status|log|build}"
        exit 1
        ;;
esac
