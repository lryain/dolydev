#!/bin/bash

###############################################################################
# ASR 模型兼容性测试脚本
# 用于测试 SenseVoice 和 Streaming Zipformer 两种模型
###############################################################################

set -e

# 颜色定义
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# 配置
ASR_SERVER_BIN="/home/pi/dolydev/libs/stt/stt-server/build/websocket_asr_server"
MODELS_ROOT="/path/to/models"  # 需要手动配置
DEFAULT_PORT=8001
SERVER_PID=""

###############################################################################
# 函数定义
###############################################################################

print_header() {
    echo -e "\n${BLUE}========== $1 ==========${NC}\n"
}

print_success() {
    echo -e "${GREEN}✓ $1${NC}"
}

print_error() {
    echo -e "${RED}✗ $1${NC}"
}

print_warn() {
    echo -e "${YELLOW}⚠ $1${NC}"
}

print_info() {
    echo -e "${BLUE}ℹ $1${NC}"
}

# 检查前置条件
check_prerequisites() {
    print_header "检查前置条件"
    
    # 检查可执行文件
    if [ ! -f "$ASR_SERVER_BIN" ]; then
        print_error "ASR 服务器可执行文件不存在: $ASR_SERVER_BIN"
        exit 1
    fi
    print_success "ASR 服务器可执行文件存在"
    
    # 检查模型根目录
    if [ ! -d "$MODELS_ROOT" ]; then
        print_warn "模型根目录不存在: $MODELS_ROOT"
        print_info "请手动设置 MODELS_ROOT 环境变量或修改脚本中的路径"
        exit 1
    fi
    print_success "模型根目录存在"
    
    # 检查必要命令
    for cmd in python3 pgrep kill; do
        if ! command -v $cmd &> /dev/null; then
            print_error "缺少命令: $cmd"
            exit 1
        fi
    done
    print_success "所有必需命令可用"
}

# 列出可用模型
list_available_models() {
    print_header "可用模型列表"
    
    if [ ! -d "$MODELS_ROOT" ]; then
        print_error "模型目录不存在"
        return
    fi
    
    local count=0
    for model_dir in "$MODELS_ROOT"/sherpa-onnx-*; do
        if [ -d "$model_dir" ]; then
            local model_name=$(basename "$model_dir")
            # 检测模型类型
            if [ -f "$model_dir/model.onnx" ]; then
                echo -e "  ${GREEN}[离线]${NC} $model_name"
                ((count++))
            elif ls "$model_dir"/encoder*.onnx > /dev/null 2>&1; then
                echo -e "  ${BLUE}[流式]${NC} $model_name"
                ((count++))
            else
                echo -e "  ${YELLOW}[未知]${NC} $model_name"
                ((count++))
            fi
        fi
    done
    
    if [ $count -eq 0 ]; then
        print_warn "未找到任何模型"
    else
        print_success "找到 $count 个模型"
    fi
}

# 启动 ASR 服务器
start_asr_server() {
    local model_name=$1
    local port=$2
    
    print_header "启动 ASR 服务器"
    print_info "模型: $model_name"
    print_info "端口: $port"
    
    # 杀死已有的进程
    if pgrep -f "websocket_asr_server" > /dev/null 2>&1; then
        print_warn "检测到已运行的 ASR 服务器，正在停止..."
        pkill -f "websocket_asr_server"
        sleep 2
    fi
    
    # 启动服务器
    LD_LIBRARY_PATH="$LD_LIBRARY_PATH:/.doly/libs/opencv/lib:/usr/local/lib" \
    "$ASR_SERVER_BIN" \
        --models-root "$MODELS_ROOT" \
        --asr-model "$model_name" \
        --port "$port" \
        --asr-debug \
        > /tmp/asr_server_$port.log 2>&1 &
    
    SERVER_PID=$!
    print_info "服务器 PID: $SERVER_PID"
    
    # 等待服务器启动
    print_info "等待服务器启动..."
    sleep 3
    
    # 检查服务器是否运行
    if ps -p $SERVER_PID > /dev/null 2>&1; then
        print_success "ASR 服务器已启动"
        print_info "日志文件: /tmp/asr_server_$port.log"
        return 0
    else
        print_error "ASR 服务器启动失败"
        print_error "错误日志:"
        tail -20 /tmp/asr_server_$port.log
        return 1
    fi
}

# 停止 ASR 服务器
stop_asr_server() {
    if [ -n "$SERVER_PID" ] && ps -p $SERVER_PID > /dev/null 2>&1; then
        print_info "停止 ASR 服务器 (PID: $SERVER_PID)..."
        kill $SERVER_PID 2>/dev/null || true
        sleep 1
    fi
}

# 创建测试 Python 脚本
create_test_script() {
    local port=$1
    local output_file="/tmp/test_asr_client_$port.py"
    
    cat > "$output_file" << 'EOF'
import websocket
import sys
import time
import json

def test_asr_client(ws_url, audio_file=None, test_duration=5):
    """
    测试 ASR WebSocket 客户端
    """
    try:
        print(f"连接到: {ws_url}")
        ws = websocket.create_connection(ws_url, timeout=10)
        print("✓ WebSocket 连接成功")
        
        # 如果提供了音频文件，发送它
        if audio_file:
            try:
                with open(audio_file, 'rb') as f:
                    chunk_size = 4096
                    print(f"发送音频文件: {audio_file}")
                    while True:
                        chunk = f.read(chunk_size)
                        if not chunk:
                            break
                        ws.send_binary(chunk)
                        time.sleep(0.01)  # 模拟实时流
                    print("✓ 音频发送完成")
            except FileNotFoundError:
                print(f"⚠ 音频文件不存在: {audio_file}")
        else:
            # 发送测试数据
            print("发送测试音频数据...")
            # 发送 1 秒的静音（16kHz, float32）
            import struct
            sample_rate = 16000
            duration = test_duration
            samples = [0.0] * (sample_rate * duration)
            audio_bytes = b''.join(struct.pack('f', s) for s in samples)
            ws.send_binary(audio_bytes)
            print("✓ 测试数据发送完成")
        
        # 接收结果
        print("等待识别结果...")
        try:
            result = ws.recv()
            if isinstance(result, str):
                print("✓ 收到识别结果:")
                try:
                    data = json.loads(result)
                    print(json.dumps(data, indent=2, ensure_ascii=False))
                except:
                    print(result)
            else:
                print(f"收到二进制数据: {len(result)} 字节")
        except websocket.WebSocketTimeoutException:
            print("⚠ 等待结果超时")
        
        ws.close()
        print("✓ 连接关闭")
        return True
        
    except Exception as e:
        print(f"✗ 错误: {e}")
        import traceback
        traceback.print_exc()
        return False

if __name__ == "__main__":
    port = sys.argv[1] if len(sys.argv) > 1 else "8001"
    wsurl = f"ws://localhost:{port}/sttRealtime"
    audio_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    success = test_asr_client(wsurl, audio_file)
    sys.exit(0 if success else 1)
EOF
    
    echo "$output_file"
}

# 测试连接
test_connection() {
    local port=$1
    
    print_header "测试 ASR 连接"
    print_info "创建测试脚本..."
    local test_script=$(create_test_script $port)
    print_success "测试脚本已创建: $test_script"
    
    print_info "运行测试..."
    python3 "$test_script" "$port"
}

# 测试两种模型
test_both_models() {
    print_header "双模型兼容性测试"
    
    local sense_voice_model="sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17"
    local zipformer_model="sherpa-onnx-streaming-zipformer-bilingual-zh-en-2023-02-20"
    
    # 检查模型是否存在
    if [ ! -d "$MODELS_ROOT/$sense_voice_model" ]; then
        print_warn "SenseVoice 模型不存在: $sense_voice_model"
    else
        print_info "测试 SenseVoice 模型..."
        if start_asr_server "$sense_voice_model" 8001; then
            test_connection 8001
        fi
        stop_asr_server
        sleep 2
    fi
    
    if [ ! -d "$MODELS_ROOT/$zipformer_model" ]; then
        print_warn "Zipformer 模型不存在: $zipformer_model"
    else
        print_info "测试 Zipformer 模型..."
        if start_asr_server "$zipformer_model" 8002; then
            test_connection 8002
        fi
        stop_asr_server
        sleep 2
    fi
    
    print_success "双模型兼容性测试完成"
}

# 显示帮助
show_help() {
    cat << 'EOF'
用法: ./test_model_compatibility.sh [命令] [选项]

命令:
  check              检查前置条件
  list               列出可用模型
  start <model>      启动指定模型的 ASR 服务器
  test <port>        测试 ASR 连接
  both               测试两种模型（完整测试）
  help               显示此帮助信息

选项:
  --models-root PATH 指定模型根目录 (默认: /path/to/models)
  --port PORT        指定服务器端口 (默认: 8001)

示例:
  ./test_model_compatibility.sh check
  ./test_model_compatibility.sh --models-root ./models list
  ./test_model_compatibility.sh --models-root ./models start \
    "sherpa-onnx-sense-voice-zh-en-ja-ko-yue-2024-07-17"
  ./test_model_compatibility.sh both

EOF
}

###############################################################################
# 主程序
###############################################################################

# 解析命令行参数
while [[ $# -gt 0 ]]; do
    case $1 in
        --models-root)
            MODELS_ROOT="$2"
            shift 2
            ;;
        --port)
            DEFAULT_PORT="$2"
            shift 2
            ;;
        check)
            check_prerequisites
            exit 0
            ;;
        list)
            check_prerequisites
            list_available_models
            exit 0
            ;;
        start)
            check_prerequisites
            if [ -z "$2" ]; then
                print_error "请指定模型名称"
                show_help
                exit 1
            fi
            if start_asr_server "$2" "$DEFAULT_PORT"; then
                print_info "服务器运行中，按 Ctrl+C 停止"
                trap "stop_asr_server" EXIT
                sleep infinity
            fi
            ;;
        test)
            test_connection "${2:-$DEFAULT_PORT}"
            exit 0
            ;;
        both)
            check_prerequisites
            test_both_models
            exit 0
            ;;
        help|--help|-h)
            show_help
            exit 0
            ;;
        *)
            print_error "未知命令: $1"
            show_help
            exit 1
            ;;
    esac
done

# 默认显示帮助
show_help
