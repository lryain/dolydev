#!/usr/bin/env python3
"""
Drive 模块测试客户端

用途：
- 订阅 Drive 状态消息 (status.drive.*)
- 发送控制命令测试舵机/LED/电机

ZeroMQ 架构：
- SUB: tcp://localhost:5555 (订阅状态)
- PUSH: tcp://localhost:5556 (发送命令)
"""

import zmq
import json
import time
import sys
import threading

class DriveTestClient:
    def __init__(self):
        self.context = zmq.Context()
        
        # 订阅状态消息（使用 IPC socket）
        self.sub_socket = self.context.socket(zmq.SUB)
        self.sub_socket.connect("ipc:///tmp/doly_zmq.sock")
        self.sub_socket.setsockopt_string(zmq.SUBSCRIBE, "status.drive.servos")
        self.sub_socket.setsockopt_string(zmq.SUBSCRIBE, "status.drive.leds")
        self.sub_socket.setsockopt_string(zmq.SUBSCRIBE, "status.drive.motors")
        
        # 发送控制命令（使用 IPC socket）
        self.push_socket = self.context.socket(zmq.PUSH)
        self.push_socket.connect("ipc:///tmp/doly_control.sock")
        
        self.running = True
        print("✅ Drive Test Client initialized")
        print("📡 SUB: ipc:///tmp/doly_zmq.sock (status.drive.*)")
        print("📤 PUSH: ipc:///tmp/doly_control.sock (io.pca9535.control)")
    
    def start_listener(self):
        """后台线程：订阅状态消息"""
        def listen():
            poller = zmq.Poller()
            poller.register(self.sub_socket, zmq.POLLIN)
            
            while self.running:
                try:
                    socks = dict(poller.poll(1000))  # 1s timeout
                    if self.sub_socket in socks:
                        topic = self.sub_socket.recv_string()
                        message = self.sub_socket.recv_string()
                        data = json.loads(message)
                        
                        # 简化输出
                        if "servos" in topic:
                            print(f"[STATUS] 舵机活跃: {data.get('active', False)}")
                        elif "leds" in topic:
                            r, g, b = data.get('r', 0), data.get('g', 0), data.get('b', 0)
                            print(f"[STATUS] LED颜色: RGB({r}, {g}, {b})")
                        elif "motors" in topic:
                            pid = data.get('pid_enabled', False)
                            print(f"[STATUS] 电机PID: {pid}")
                except Exception as e:
                    print(f"❌ 订阅错误: {e}")
        
        thread = threading.Thread(target=listen, daemon=True)
        thread.start()
        print("🎧 状态监听线程已启动")
    
    def send_command(self, action, **kwargs):
        """发送 ZeroMQ 命令"""
        cmd = {"action": action}
        cmd.update(kwargs)
        
        topic = "io.pca9535.control"  # 使用正确的 topic
        message = json.dumps(cmd)
        
        self.push_socket.send_string(topic, zmq.SNDMORE)
        self.push_socket.send_string(message)
        print(f"✅ 发送命令: {action} {kwargs}")
    
    def test_servo(self):
        """测试舵机"""
        print("\n=== 舵机测试 ===")
        print("1. 左舵机 90°")
        self.send_command("set_servo", channel="left", angle=90, speed=50)
        time.sleep(2)
        
        print("2. 右舵机 120°")
        self.send_command("set_servo", channel="right", angle=120, speed=50)
        time.sleep(2)
        
        print("3. 左舵机 0°")
        self.send_command("set_servo", channel="left", angle=0, speed=30)
        time.sleep(2)
        
        print("4. 停止所有舵机")
        self.send_command("stop_servo", channel="all")
    
    def test_led(self):
        """测试 LED"""
        print("\n=== LED 测试 ===")
        print("1. 红色")
        self.send_command("set_led_color", r=255, g=0, b=0)
        time.sleep(2)
        
        print("2. 绿色")
        self.send_command("set_led_color", r=0, g=255, b=0)
        time.sleep(2)
        
        print("3. 蓝色")
        self.send_command("set_led_color", r=0, g=0, b=255)
        time.sleep(2)
        
        print("4. 青色呼吸")
        self.send_command("set_led_effect", effect="breath", r=0, g=255, b=255)
        time.sleep(5)
        
        print("5. 红色闪烁")
        self.send_command("set_led_effect", effect="blink", r=255, g=0, b=0)
        time.sleep(5)
        
        print("6. 关闭 LED")
        self.send_command("led_off")
    
    def test_motor(self):
        """测试电机"""
        print("\n=== 电机测试 ===")
        print("⚠️ 警告：确保机器人已抬起或在安全区域！")
        input("按回车继续...")
        
        print("1. 前进 (速度0.3, 1秒)")
        self.send_command("motor_forward", speed=0.3, duration=1.0)
        time.sleep(2)
        
        print("2. 后退 (速度0.3, 1秒)")
        self.send_command("motor_backward", speed=0.3, duration=1.0)
        time.sleep(2)
        
        print("3. 左转 (速度0.3, 0.5秒)")
        self.send_command("motor_turn_left", speed=0.3, duration=0.5)
        time.sleep(2)
        
        print("4. 右转 (速度0.3, 0.5秒)")
        self.send_command("motor_turn_right", speed=0.3, duration=0.5)
        time.sleep(2)
        
        print("5. 停止")
        self.send_command("motor_stop")
    
    def interactive_mode(self):
        """交互模式"""
        print("\n=== 交互模式 ===")
        print("命令列表:")
        print("  servo <left|right> <angle> [speed] - 设置舵机角度")
        print("  led <r> <g> <b>                   - 设置LED颜色")
        print("  breath <r> <g> <b>                - LED呼吸效果")
        print("  blink <r> <g> <b>                 - LED闪烁效果")
        print("  motor <left> <right> [duration]   - 设置电机速度")
        print("  stop                              - 停止所有")
        print("  quit                              - 退出")
        
        while self.running:
            try:
                cmd_line = input("\n>>> ").strip()
                if not cmd_line:
                    continue
                
                parts = cmd_line.split()
                cmd = parts[0].lower()
                
                if cmd == "quit":
                    break
                elif cmd == "servo" and len(parts) >= 3:
                    channel = parts[1]
                    angle = float(parts[2])
                    speed = int(parts[3]) if len(parts) > 3 else 50
                    self.send_command("set_servo", channel=channel, angle=angle, speed=speed)
                
                elif cmd == "led" and len(parts) == 4:
                    r, g, b = int(parts[1]), int(parts[2]), int(parts[3])
                    self.send_command("set_led_color", r=r, g=g, b=b)
                
                elif cmd == "breath" and len(parts) == 4:
                    r, g, b = int(parts[1]), int(parts[2]), int(parts[3])
                    self.send_command("set_led_effect", effect="breath", r=r, g=g, b=b)
                
                elif cmd == "blink" and len(parts) == 4:
                    r, g, b = int(parts[1]), int(parts[2]), int(parts[3])
                    self.send_command("set_led_effect", effect="blink", r=r, g=g, b=b)
                
                elif cmd == "motor" and len(parts) >= 3:
                    left = float(parts[1])
                    right = float(parts[2])
                    duration = float(parts[3]) if len(parts) > 3 else 0.0
                    self.send_command("set_motor_speed", left=left, right=right, duration=duration)
                
                elif cmd == "stop":
                    self.send_command("motor_stop")
                    self.send_command("stop_servo", channel="all")
                    self.send_command("led_off")
                
                else:
                    print("❌ 未知命令或参数错误")
                
            except KeyboardInterrupt:
                break
            except Exception as e:
                print(f"❌ 错误: {e}")
    
    def run_all_tests(self):
        """运行所有测试"""
        self.start_listener()
        time.sleep(1)  # 等待监听线程启动
        
        try:
            self.test_servo()
            # self.test_led()
            self.test_motor()  # 需要确认硬件安全
            
        except KeyboardInterrupt:
            print("\n测试中断")
    
    def cleanup(self):
        """清理资源"""
        self.running = False
        self.sub_socket.close()
        self.push_socket.close()
        self.context.term()
        print("\n✅ 客户端已关闭")


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "interactive":
        # 交互模式
        client = DriveTestClient()
        client.start_listener()
        time.sleep(1)
        try:
            client.interactive_mode()
        finally:
            client.cleanup()
    else:
        # 自动测试模式
        client = DriveTestClient()
        try:
            client.run_all_tests()
            print("\n✅ 所有测试完成")
        except KeyboardInterrupt:
            print("\n测试中断")
        finally:
            client.cleanup()


if __name__ == "__main__":
    main()
