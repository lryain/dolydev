#!/usr/bin/env python3
"""
PWM补偿值测试程序

测试不同PWM补偿值对电机驱动的影响。
通过改变配置文件中的补偿值，观察电机响应和日志输出。
"""

import zmq
import json
import time
import sys
import os
import subprocess
import signal
from pathlib import Path

def get_project_root():
    """获取项目根目录"""
    return Path(__file__).parent.parent.parent.parent

def get_config_file():
    """获取电机配置文件路径"""
    return get_project_root() / "config" / "motor_config.ini"

def get_drive_log():
    """获取drive日志文件路径"""
    return get_project_root() / "libs" / "drive" / "drive.log"

def kill_existing_service():
    """杀死已存在的drive_service进程"""
    try:
        os.system("sudo pkill -f -9 drive_service")
        time.sleep(1)
    except:
        pass

def start_drive_service():
    """启动drive_service"""
    build_dir = get_project_root() / "libs" / "drive" / "build"
    service_path = build_dir / "drive_service"
    
    if not service_path.exists():
        print(f"❌ drive_service 不存在: {service_path}")
        return None
    
    log_file = get_drive_log()
    log_file.parent.mkdir(exist_ok=True)
    
    # 清空旧日志
    log_file.write_text("")
    
    print(f"🚀 启动 drive_service: {service_path}")
    print(f"📝 日志文件: {log_file}")
    
    # 启动进程
    process = subprocess.Popen(
        [str(service_path)],
        stdout=open(log_file, 'w'),
        stderr=subprocess.STDOUT,
        preexec_fn=os.setsid
    )
    
    time.sleep(2)  # 等待服务启动
    return process

def stop_drive_service(process):
    """停止drive_service"""
    if process:
        try:
            os.killpg(os.getpgid(process.pid), signal.SIGTERM)
            time.sleep(1)
        except:
            pass

def read_config(config_file):
    """读取配置文件"""
    config = {}
    with open(config_file, 'r') as f:
        current_section = None
        for line in f:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            if line.startswith('['):
                current_section = line[1:-1]
                continue
            if '=' in line:
                key, value = line.split('=', 1)
                key = key.strip()
                value = value.strip()
                if current_section not in config:
                    config[current_section] = {}
                config[current_section][key] = value
    return config

def write_pwm_compensation(config_file, left_comp, right_comp):
    """修改PWM补偿值配置"""
    lines = []
    with open(config_file, 'r') as f:
        lines = f.readlines()
    
    new_lines = []
    for i, line in enumerate(lines):
        stripped = line.strip()
        if stripped.startswith('left_pwm_compensation'):
            new_lines.append(f'left_pwm_compensation = {left_comp}\n')
        elif stripped.startswith('right_pwm_compensation'):
            new_lines.append(f'right_pwm_compensation = {right_comp}\n')
        else:
            new_lines.append(line)
    
    with open(config_file, 'w') as f:
        f.writelines(new_lines)
    
    print(f"✏️  已设置 left_pwm_compensation={left_comp}, right_pwm_compensation={right_comp}")

def send_motor_command(ctx, action, duration=1.0, speed=0.3):
    """发送电机命令"""
    try:
        push = ctx.socket(zmq.PUSH)
        push.setsockopt(zmq.LINGER, 0)
        push.connect('ipc:///tmp/doly_control.sock')
        
        time.sleep(0.5)
        
        if action == "forward":
            cmd = {"action": "motor_forward", "speed": speed, "duration": duration}
        elif action == "backward":
            cmd = {"action": "motor_backward", "speed": speed, "duration": duration}
        elif action == "left":
            cmd = {"action": "motor_turn_left", "speed": speed, "duration": duration}
        elif action == "right":
            cmd = {"action": "motor_turn_right", "speed": speed, "duration": duration}
        elif action == "stop":
            cmd = {"action": "motor_stop"}
        else:
            print(f"❌ 未知命令: {action}")
            push.close()
            return False
        
        push.send_string("io.pca9535.control", zmq.SNDMORE)
        push.send_string(json.dumps(cmd))
        print(f"📤 发送命令: {action} (speed={speed}, duration={duration}s)")
        
        push.close()
        return True
    except Exception as e:
        print(f"❌ 发送命令失败: {e}")
        return False

def read_recent_log(log_file, lines=20):
    """读取最近的日志"""
    try:
        with open(log_file, 'r') as f:
            all_lines = f.readlines()
            return all_lines[-lines:]
    except:
        return []

def test_pwm_compensation():
    """测试PWM补偿值"""
    config_file = get_config_file()
    
    if not config_file.exists():
        print(f"❌ 配置文件不存在: {config_file}")
        return False
    
    print(f"📋 配置文件: {config_file}")
    
    # 初始化ZMQ
    ctx = zmq.Context()
    
    try:
        # 杀死现有的服务
        kill_existing_service()
        
        # 测试补偿值列表
        test_cases = [
            (0.8, "低补偿（更容易驱动）"),
            (0.9, "略低补偿"),
            (1.0, "标准补偿（无补偿）"),
            (1.1, "略高补偿"),
            (1.2, "高补偿（需要更高PWM）"),
        ]
        
        print("\n" + "="*60)
        print("PWM补偿值测试")
        print("="*60)
        
        for comp_value, description in test_cases:
            print(f"\n{'='*60}")
            print(f"测试: {description} (补偿值={comp_value})")
            print(f"{'='*60}")
            
            # 修改配置
            write_pwm_compensation(config_file, comp_value, comp_value)
            time.sleep(0.5)
            
            # 启动服务
            process = start_drive_service()
            
            # 发送测试命令
            print("\n📍 左转测试:")
            send_motor_command(ctx, "left", duration=2.0, speed=0.3)
            time.sleep(3)
            
            print("\n📍 右转测试:")
            send_motor_command(ctx, "right", duration=2.0, speed=0.3)
            time.sleep(3)
            
            print("\n📍 停止:")
            send_motor_command(ctx, "stop")
            time.sleep(1)
            
            # 读取日志
            log_file = get_drive_log()
            recent_log = read_recent_log(log_file, lines=30)
            
            print(f"\n📝 最近日志输出 (最后30行):")
            print("-" * 60)
            
            # 查找PWM补偿值相关的日志
            pwm_comp_logs = [line for line in recent_log if "PWM补偿" in line or "pwm_compensation" in line]
            
            if pwm_comp_logs:
                print("🔍 PWM补偿值日志:")
                for log_line in pwm_comp_logs:
                    print(f"  {log_line.rstrip()}")
            else:
                print("⚠️  未找到PWM补偿值的日志")
            
            # 显示最后的几行日志
            print("\n📋 最后日志输出:")
            for line in recent_log[-10:]:
                print(f"  {line.rstrip()}")
            
            # 停止服务
            stop_drive_service(process)
            time.sleep(1)
        
        print(f"\n{'='*60}")
        print("✅ 测试完成！")
        print(f"{'='*60}")
        
    except KeyboardInterrupt:
        print("\n⚠️  测试被中断")
        stop_drive_service(process)
    except Exception as e:
        print(f"\n❌ 测试出错: {e}")
        import traceback
        traceback.print_exc()
    finally:
        ctx.term()
    
    return True

if __name__ == "__main__":
    test_pwm_compensation()
