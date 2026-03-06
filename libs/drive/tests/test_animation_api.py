#!/usr/bin/env python3
"""
测试动画系统 API: drive_distance, drive_rotate, drive_rotate_left, drive_rotate_right
参考 quick_test.py 编码器测试例子
"""
import zmq
import json
import time
import sys
import threading
from datetime import datetime

# 日志配置
LOG_PREFIX = "[AnimationAPITest]"

class DriveAPITester:
    def __init__(self):
        self.ctx = zmq.Context()
        self.push = self.ctx.socket(zmq.PUSH)
        self.push.connect('ipc:///tmp/doly_control.sock')
        self.test_results = []
        print(f"{LOG_PREFIX} ✅ 连接到 ipc:///tmp/doly_control.sock")
        time.sleep(0.5)
    
    def send_command(self, action, params, name=""):
        """发送命令到 drive_service"""
        cmd = {"action": action}
        cmd.update(params)
        
        try:
            self.push.send_string("io.pca9535.control", zmq.SNDMORE)
            self.push.send_string(json.dumps(cmd))
            timestamp = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            print(f"{LOG_PREFIX} [{timestamp}] ✓ 发送: {name or action}")
            print(f"  └─ params: {json.dumps(params, ensure_ascii=False)}")
            return True
        except Exception as e:
            print(f"{LOG_PREFIX} ✗ 发送失败: {name or action} - {e}")
            self.test_results.append((name or action, False, str(e)))
            return False
    
    def test_drive_distance_basic(self):
        """测试 drive_distance 基本功能"""
        print(f"\n{LOG_PREFIX} ═════════════════════════════════════════")
        print(f"{LOG_PREFIX} 测试 1: drive_distance (基本参数)")
        print(f"{LOG_PREFIX} ═════════════════════════════════════════")
        
        tests = [
            ("前进 50mm", {
                "distance_mm": 50,
                "speed": 15,
                "accel": 0,
                "brake": 0,
                "direction": 0
            }),
            ("前进 100mm 带加速", {
                "distance_mm": 100,
                "speed": 25,
                "accel": 30,
                "brake": 0,
                "direction": 0
            }),
            ("前进 80mm 带加速和刹车", {
                "distance_mm": 80,
                "speed": 20,
                "accel": 20,
                "brake": 20,
                "direction": 0
            }),
            ("后退 50mm", {
                "distance_mm": 50,
                "speed": 15,
                "accel": 0,
                "brake": 0,
                "direction": 1
            }),
            ("后退 100mm 带加速刹车", {
                "distance_mm": 100,
                "speed": 25,
                "accel": 25,
                "brake": 25,
                "direction": 1
            }),
        ]
        
        for name, params in tests:
            self.send_command("drive_distance", params, name)
            time.sleep(2.5)  # 等待完成
        
        print(f"{LOG_PREFIX} ✓ drive_distance 测试完成")
    
    def test_drive_rotate_center(self):
        """测试 drive_rotate 原地旋转模式"""
        print(f"\n{LOG_PREFIX} ═════════════════════════════════════════")
        print(f"{LOG_PREFIX} 测试 2: drive_rotate (原地转向, is_center=true)")
        print(f"{LOG_PREFIX} ═════════════════════════════════════════")
        
        tests = [
            ("原地左转 45°", {
                "angle_deg": 45,
                "speed": 20,
                "is_center": True
            }),
            ("原地右转 45°", {
                "angle_deg": -45,
                "speed": 20,
                "is_center": True
            }),
            ("原地左转 90°", {
                "angle_deg": 90,
                "speed": 20,
                "is_center": True
            }),
            ("原地右转 90°", {
                "angle_deg": -90,
                "speed": 20,
                "is_center": True
            }),
            ("原地左转 180°", {
                "angle_deg": 180,
                "speed": 15,
                "is_center": True
            }),
        ]
        
        for name, params in tests:
            self.send_command("drive_rotate", params, name)
            time.sleep(2.5)  # 等待完成
        
        print(f"{LOG_PREFIX} ✓ drive_rotate (center) 测试完成")
    
    def test_drive_rotate_wheel(self):
        """测试 drive_rotate 以轮为中心旋转模式"""
        print(f"\n{LOG_PREFIX} ═════════════════════════════════════════")
        print(f"{LOG_PREFIX} 测试 3: drive_rotate (以轮为中心, is_center=false)")
        print(f"{LOG_PREFIX} ═════════════════════════════════════════")
        
        tests = [
            ("左转 45° (单轮轴心)", {
                "angle_deg": 45,
                "speed": 20,
                "is_center": False
            }),
            ("右转 45° (单轮轴心)", {
                "angle_deg": -45,
                "speed": 20,
                "is_center": False
            }),
            ("左转 90° (单轮轴心)", {
                "angle_deg": 90,
                "speed": 20,
                "is_center": False
            }),
        ]
        
        for name, params in tests:
            self.send_command("drive_rotate", params, name)
            time.sleep(2.5)
        
        print(f"{LOG_PREFIX} ✓ drive_rotate (wheel) 测试完成")
    
    def test_drive_rotate_left_right(self):
        """测试 drive_rotate_left 和 drive_rotate_right 便捷函数"""
        print(f"\n{LOG_PREFIX} ═════════════════════════════════════════")
        print(f"{LOG_PREFIX} 测试 4: drive_rotate_left & drive_rotate_right")
        print(f"{LOG_PREFIX} ═════════════════════════════════════════")
        
        tests = [
            ("drive_rotate_left 45°", {
                "angle_deg": 45,
                "speed": 20,
                "is_center": True
            }, "drive_rotate_left"),
            ("drive_rotate_right 45°", {
                "angle_deg": 45,
                "speed": 20,
                "is_center": True
            }, "drive_rotate_right"),
            ("drive_rotate_left 90°", {
                "angle_deg": 90,
                "speed": 20,
                "is_center": True
            }, "drive_rotate_left"),
            ("drive_rotate_right 90°", {
                "angle_deg": 90,
                "speed": 20,
                "is_center": True
            }, "drive_rotate_right"),
        ]
        
        for name, params, action in tests:
            self.send_command(action, params, name)
            time.sleep(2.5)
        
        print(f"{LOG_PREFIX} ✓ drive_rotate_left/right 测试完成")
    
    def test_compatibility_params(self):
        """测试参数兼容性 (distance vs distance_mm, driveRotate vs angle_deg)"""
        print(f"\n{LOG_PREFIX} ═════════════════════════════════════════")
        print(f"{LOG_PREFIX} 测试 5: 参数兼容性 (XML中使用的字段名)")
        print(f"{LOG_PREFIX} ═════════════════════════════════════════")
        
        tests = [
            ("drive_distance 使用 'distance' 字段", {
                "distance": 60,  # 注意：不是 distance_mm
                "speed": 20,
                "accel": 0,
                "brake": 0,
                "direction": 0
            }, "drive_distance"),
            ("drive_rotate 使用 'driveRotate' 字段", {
                "driveRotate": 45,  # 注意：不是 angle_deg
                "speed": 20,
                "isCenter": True  # 注意：不是 is_center
            }, "drive_rotate"),
            ("drive_rotate_left 使用 'driveRotate' 字段", {
                "driveRotate": 45,
                "speed": 20,
                "isCenter": True
            }, "drive_rotate_left"),
        ]
        
        for name, params, action in tests:
            self.send_command(action, params, name)
            time.sleep(2.5)
        
        print(f"{LOG_PREFIX} ✓ 参数兼容性测试完成")
    
    def test_combined_sequence(self):
        """测试组合序列：前进 -> 左转 -> 前进 -> 右转"""
        print(f"\n{LOG_PREFIX} ═════════════════════════════════════════")
        print(f"{LOG_PREFIX} 测试 6: 组合动作序列")
        print(f"{LOG_PREFIX} ═════════════════════════════════════════")
        
        sequence = [
            ("前进 100mm", {
                "distance_mm": 100,
                "speed": 20,
                "accel": 20,
                "brake": 20,
                "direction": 0
            }, "drive_distance"),
            ("左转 90°", {
                "angle_deg": 90,
                "speed": 20,
                "is_center": True
            }, "drive_rotate_left"),
            ("前进 50mm", {
                "distance_mm": 50,
                "speed": 20,
                "accel": 0,
                "brake": 0,
                "direction": 0
            }, "drive_distance"),
            ("右转 90°", {
                "angle_deg": 90,
                "speed": 20,
                "is_center": True
            }, "drive_rotate_right"),
        ]
        
        for name, params, action in sequence:
            self.send_command(action, params, name)
            time.sleep(2.5)
        
        print(f"{LOG_PREFIX} ✓ 组合动作序列完成")
    
    def run_all_tests(self):
        """运行所有测试"""
        print(f"\n{LOG_PREFIX} ╔════════════════════════════════════════════╗")
        print(f"{LOG_PREFIX} ║   动画系统 Motor API 完整测试套件          ║")
        print(f"{LOG_PREFIX} ║   Start Time: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}          ║")
        print(f"{LOG_PREFIX} ╚════════════════════════════════════════════╝")
        
        try:
            self.test_drive_distance_basic()
            self.test_drive_rotate_center()
            self.test_drive_rotate_wheel()
            self.test_drive_rotate_left_right()
            self.test_compatibility_params()
            self.test_combined_sequence()
            
            print(f"\n{LOG_PREFIX} ╔════════════════════════════════════════════╗")
            print(f"{LOG_PREFIX} ║   ✓ 所有测试完成                           ║")
            print(f"{LOG_PREFIX} ║   End Time: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}          ║")
            print(f"{LOG_PREFIX} ╚════════════════════════════════════════════╝")
            
        except KeyboardInterrupt:
            print(f"\n{LOG_PREFIX} ✗ 测试被中断")
        except Exception as e:
            print(f"\n{LOG_PREFIX} ✗ 测试异常: {e}")
            import traceback
            traceback.print_exc()
        finally:
            self.push.close()
            self.ctx.term()


def main():
    tester = DriveAPITester()
    tester.run_all_tests()


if __name__ == "__main__":
    main()
