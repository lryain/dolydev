#!/usr/bin/env python3
"""
快速测试 PWM 补偿值
通过这个脚本可以快速测试不同的PWM补偿值对电机驱动的影响
"""
import zmq
import json
import time
import sys
import os
from pathlib import Path

def get_config_file():
    """获取配置文件路径"""
    return Path(__file__).parent.parent.parent / "config" / "motor_config.ini"

def get_drive_log():
    """获取drive日志文件路径"""
    return Path(__file__).parent.parent / "drive.log"

def read_pwm_compensation():
    """读取当前的PWM补偿值"""
    config_file = get_config_file()
    left_comp = 1.0
    right_comp = 1.0
    
    try:
        with open(config_file, 'r') as f:
            for line in f:
                line = line.strip()
                if line.startswith('left_pwm_compensation'):
                    left_comp = float(line.split('=')[1].strip())
                elif line.startswith('right_pwm_compensation'):
                    right_comp = float(line.split('=')[1].strip())
    except Exception as e:
        print(f"❌ 读取配置失败: {e}")
    
    return left_comp, right_comp

def set_pwm_compensation(left, right):
    """设置PWM补偿值"""
    config_file = get_config_file()
    
    try:
        with open(config_file, 'r') as f:
            lines = f.readlines()
        
        new_lines = []
        for line in lines:
            if line.strip().startswith('left_pwm_compensation'):
                new_lines.append(f'left_pwm_compensation = {left}\n')
            elif line.strip().startswith('right_pwm_compensation'):
                new_lines.append(f'right_pwm_compensation = {right}\n')
            else:
                new_lines.append(line)
        
        with open(config_file, 'w') as f:
            f.writelines(new_lines)
        
        print(f"✅ PWM补偿值已设置: left={left}, right={right}")
        return True
    except Exception as e:
        print(f"❌ 设置补偿值失败: {e}")
        return False

def send_motor_command(action, duration=1.0, speed=0.3):
    """发送电机命令"""
    try:
        ctx = zmq.Context()
        push = ctx.socket(zmq.PUSH)
        push.setsockopt(zmq.LINGER, 0)
        push.connect('ipc:///tmp/doly_control.sock')
        time.sleep(0.2)
        
        # 构建命令
        if action == "left":
            cmd = {"action": "motor_turn_left", "speed": speed, "duration": duration}
        elif action == "right":
            cmd = {"action": "motor_turn_right", "speed": speed, "duration": duration}
        elif action == "forward":
            cmd = {"action": "motor_forward", "speed": speed, "duration": duration}
        elif action == "backward":
            cmd = {"action": "motor_backward", "speed": speed, "duration": duration}
        elif action == "stop":
            cmd = {"action": "motor_stop"}
        else:
            print(f"❌ 未知命令: {action}")
            push.close()
            ctx.term()
            return False
        
        # 发送命令
        push.send_string("io.pca9535.control", zmq.SNDMORE)
        push.send_string(json.dumps(cmd))
        
        push.close()
        ctx.term()
        return True
    except Exception as e:
        print(f"❌ 发送命令失败: {e}")
        return False

def show_menu():
    """显示菜单"""
    left, right = read_pwm_compensation()
    
    print("\n" + "="*60)
    print("PWM补偿值快速测试")
    print("="*60)
    print(f"当前PWM补偿值: left={left}, right={right}")
    print("\n选项:")
    print("  1 - 测试 left=0.8 (低补偿)")
    print("  2 - 测试 left=0.9 (略低)")
    print("  3 - 测试 left=1.0 (标准，无补偿)")
    print("  4 - 测试 left=1.1 (略高)")
    print("  5 - 测试 left=1.2 (高补偿)")
    print("  6 - 自定义值")
    print("  7 - 显示日志")
    print("  8 - 退出")
    print("="*60)

def show_drive_log():
    """显示drive日志的最后部分"""
    log_file = get_drive_log()
    
    try:
        if log_file.exists():
            with open(log_file, 'r') as f:
                lines = f.readlines()
            
            print("\n📝 Drive 日志 (最后30行):")
            print("-"*60)
            
            # 过滤PWM补偿相关的日志
            relevant_lines = []
            for line in lines:
                if any(keyword in line for keyword in 
                       ['PWM补偿', 'pwm_compensation', '⚙️', 'LCD', '电机', '驱动']):
                    relevant_lines.append(line)
            
            if relevant_lines:
                print("🔍 相关日志:")
                for line in relevant_lines[-20:]:
                    print(f"  {line.rstrip()}")
            else:
                print("⚠️  未找到相关日志")
            
            print("\n📋 最后日志:")
            for line in lines[-10:]:
                print(f"  {line.rstrip()}")
        else:
            print(f"❌ 日志文件不存在: {log_file}")
    except Exception as e:
        print(f"❌ 读取日志失败: {e}")
    
    print("-"*60)

def test_compensation(value):
    """测试指定的补偿值"""
    print(f"\n🔧 设置PWM补偿值: {value}")
    
    if not set_pwm_compensation(value, value):
        return
    
    print("\n⏳ 重启drive_service后需要手动测试电机")
    print("建议:")
    print("  1. 手动重启drive_service")
    print("  2. 推送电机转弯命令观察响应效果")
    print("  3. 根据观察结果调整补偿值")
    print("\n测试命令:")
    print("  send_motor_command('left', duration=2.0, speed=0.3)  # 左转")
    print("  send_motor_command('right', duration=2.0, speed=0.3) # 右转")

def main():
    """主程序"""
    print("🚀 PWM补偿值快速测试工具")
    
    while True:
        show_menu()
        choice = input("请选择 (1-8): ").strip()
        
        if choice == "1":
            test_compensation(0.8)
        elif choice == "2":
            test_compensation(0.9)
        elif choice == "3":
            test_compensation(1.0)
        elif choice == "4":
            test_compensation(1.1)
        elif choice == "5":
            test_compensation(1.2)
        elif choice == "6":
            try:
                value = float(input("请输入补偿值 (0.0-2.0): "))
                if 0.0 <= value <= 2.0:
                    test_compensation(value)
                else:
                    print("❌ 值超出范围 (0.0-2.0)")
            except ValueError:
                print("❌ 无效的数值")
        elif choice == "7":
            show_drive_log()
        elif choice == "8":
            print("👋 再见！")
            break
        else:
            print("❌ 无效选择")

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n⚠️  已中断")
