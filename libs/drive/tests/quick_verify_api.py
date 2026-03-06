#!/usr/bin/env python3
"""
快速验证 - 测试 move_distance_cm 和 turn_deg API
执行简单的前进和转向动作
"""
import zmq
import json
import time
import sys

def send_command(sock, action, params, desc=""):
    """发送命令"""
    cmd = {"action": action}
    cmd.update(params)
    
    sock.send_string("io.pca9535.control", zmq.SNDMORE)
    sock.send_string(json.dumps(cmd))
    
    if desc:
        print(f"✅ {desc}")
        print(f"   {json.dumps(cmd, indent=2)}")

def main():
    print("╔══════════════════════════════════════════════════════════════╗")
    print("║       快速验证 - 动画系统电机 API                            ║")
    print("╚══════════════════════════════════════════════════════════════╝")
    print()
    print("⚠️  确保:")
    print("  1. drive_service 已启动")
    print("  2. Doly 放在安全区域")
    print("  3. 准备好紧急停止 (Ctrl+C)")
    print()
    
    input("按回车键开始测试... ")
    print()
    
    # 连接 ZMQ
    ctx = zmq.Context()
    sock = ctx.socket(zmq.PUSH)
    sock.connect('ipc:///tmp/doly_control.sock')
    print("✅ 已连接到 ipc:///tmp/doly_control.sock")
    time.sleep(0.5)
    
    try:
        # 测试1: 前进 5cm
        # print("\n" + "="*60)
        # print("🎯 测试 1: 前进 5cm (speed=20%)")
        # print("="*60)
        # send_command(sock, "move_distance_cm", {
        #     "distance_cm": 5.0,
        #     "throttle": 0.2,
        #     "timeout_ms": 3000
        # }, "执行: 前进 5cm")
        # print("⏳ 等待 3 秒...")
        # time.sleep(3)
        
        # # 测试2: 后退 5cm
        # print("\n" + "="*60)
        # print("🎯 测试 2: 后退 5cm (speed=20%)")
        # print("="*60)
        # send_command(sock, "move_distance_cm", {
        #     "distance_cm": -5.0,
        #     "throttle": 0.2,
        #     "timeout_ms": 3000
        # }, "执行: 后退 5cm")
        # print("⏳ 等待 3 秒...")
        # time.sleep(3)
        
        # 测试3: 右转 45°
        print("\n" + "="*60)
        print("🎯 测试 3: 右转 45° (speed=20%)")
        print("="*60)
        send_command(sock, "turn_deg", {
            "angle_deg": 45.0,
            "throttle": 0.2,
            "timeout_ms": 3000
        }, "执行: 右转 45°")
        print("⏳ 等待 3 秒...")
        time.sleep(3)
        
        # 测试4: 左转 45° (恢复)
        print("\n" + "="*60)
        print("🎯 测试 4: 左转 45° (恢复)")
        print("="*60)
        send_command(sock, "turn_deg", {
            "angle_deg": -45.0,
            "throttle": 0.2,
            "timeout_ms": 3000
        }, "执行: 左转 45°")
        print("⏳ 等待 3 秒...")
        time.sleep(3)

        print("\n" + "="*60)
        print("🎯 测试 4: 左转 45° (恢复)")
        print("="*60)
        send_command(sock, "turn_deg", {
            "angle_deg": -45.0,
            "throttle": 0.2,
            "timeout_ms": 3000
        }, "执行: 左转 45°")
        print("⏳ 等待 3 秒...")
        time.sleep(3)
        
        # 停止
        print("\n" + "="*60)
        print("🛑 停止")
        print("="*60)
        send_command(sock, "motor_stop", {}, "执行: 停止电机")
        
        print("\n" + "="*60)
        print("✅ 快速验证完成！")
        print("="*60)
        print()
        print("📊 查看详细日志:")
        print("  tail -50 /home/pi/dolydev/libs/drive/drive.log")
        print()
        
    except KeyboardInterrupt:
        print("\n\n❌ 测试被中断")
        send_command(sock, "motor_stop", {}, "紧急停止")
    except Exception as e:
        print(f"\n\n❌ 错误: {e}")
        import traceback
        traceback.print_exc()
        send_command(sock, "motor_stop", {}, "紧急停止")
    finally:
        sock.close()
        ctx.term()

if __name__ == "__main__":
    main()
