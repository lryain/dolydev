#!/bin/bash
# Vision Service 快速测试脚本
# 用法: ./quick_test.sh

set -e

echo "╔═══════════════════════════════════════════════════╗"
echo "║  Vision Service 快速测试                         ║"
echo "╚═══════════════════════════════════════════════════╝"
echo ""

# 颜色定义
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
RED='\033[0;31m'
NC='\033[0m' # No Color

# 1. 检查文件是否存在
echo "📋 Step 1: 检查文件..."
if [ -f "/home/pi/dolydev/libslibs/FaceReco/build/LiveFaceReco" ]; then
    echo -e "${GREEN}✓${NC} LiveFaceReco 可执行文件存在"
else
    echo -e "${RED}✗${NC} LiveFaceReco 未找到，请先编译"
    exit 1
fi

# 2. 停止旧进程
echo ""
echo "🛑 Step 2: 停止旧进程..."
sudo pkill -9 -f LiveFaceReco 2>/dev/null || true
sleep 2
echo -e "${GREEN}✓${NC} 旧进程已停止"

# 3. 启动 LiveFaceReco
echo ""
echo "🚀 Step 3: 启动 LiveFaceReco..."
cd /home/pi/dolydev/libslibs/FaceReco/build
./LiveFaceReco > /tmp/livefacereco_test.log 2>&1 &
FACERECO_PID=$!
sleep 5

if ps -p $FACERECO_PID > /dev/null; then
    echo -e "${GREEN}✓${NC} LiveFaceReco 已启动 (PID: $FACERECO_PID)"
else
    echo -e "${RED}✗${NC} LiveFaceReco 启动失败"
    echo "查看日志: cat /tmp/livefacereco_test.log"
    exit 1
fi

# 4. 检查 FaceDatabase 日志
echo ""
echo "📦 Step 4: 检查 FaceDatabase 加载..."
sleep 2
if grep -q "FaceDB" /tmp/livefacereco_test.log; then
    echo -e "${GREEN}✓${NC} FaceDatabase 日志:"
    grep "FaceDB" /tmp/livefacereco_test.log | head -10
else
    echo -e "${YELLOW}⚠${NC}  未找到 FaceDatabase 日志"
fi

# 5. 检查 IDLE 模式日志
echo ""
echo "🎯 Step 5: 检查 IDLE 模式..."
if grep -q "初始模式" /tmp/livefacereco_test.log; then
    echo -e "${GREEN}✓${NC} 模式配置日志:"
    grep -E "初始模式|模块管理器" /tmp/livefacereco_test.log
else
    echo -e "${YELLOW}⚠${NC}  未找到模式配置日志"
fi

# 6. 测试 set_gaze 命令（需要 eyeEngine）
echo ""
echo "👁️  Step 6: 测试 set_gaze 命令..."
if pgrep -f "zmq_service" > /dev/null; then
    echo -e "${GREEN}✓${NC} eyeEngine 正在运行"
    
    # 创建测试脚本
    cat > /tmp/test_gaze.py << 'EOF'
import zmq
import json

context = zmq.Context()
socket = context.socket(zmq.REQ)
socket.setsockopt(zmq.RCVTIMEO, 3000)
socket.connect("ipc:///tmp/doly_eye_cmd.sock")

try:
    cmd = {"action": "set_gaze", "x": -0.5, "y": 0.3, "priority": 5}
    socket.send_json(cmd)
    response = socket.recv_json()
    print(f"SUCCESS: {response}")
except Exception as e:
    print(f"ERROR: {e}")
finally:
    socket.close()
    context.term()
EOF
    
    echo "发送 set_gaze 命令..."
    python3 /tmp/test_gaze.py
else
    echo -e "${YELLOW}⚠${NC}  eyeEngine 未运行，跳过 set_gaze 测试"
    echo "   启动命令: cd /home/pi/dolydev/libsmodules/eyeEngine && nohup python3 zmq_service.py > zmq.log 2>&1 &"
fi

# 7. 显示完整日志位置
echo ""
echo "╔═══════════════════════════════════════════════════╗"
echo "║  测试完成                                         ║"
echo "╚═══════════════════════════════════════════════════╝"
echo ""
echo "📋 完整日志位置:"
echo "   LiveFaceReco: /tmp/livefacereco_test.log"
echo "   eyeEngine: /home/pi/dolydev/libsmodules/eyeEngine/zmq.log"
echo ""
echo "📝 查看日志命令:"
echo "   tail -f /tmp/livefacereco_test.log"
echo ""
echo "🛑 停止服务:"
echo "   sudo pkill -9 -f LiveFaceReco"
echo ""
