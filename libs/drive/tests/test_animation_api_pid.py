#!/usr/bin/env python3
"""
扩充测试程序：包含 drive_distance_pid 和 turn_deg_pid
参考 quick_test.py 编码器测试例子
"""
import zmq
import json
import time
import sys
from datetime import datetime

# 日志配置
LOG_PREFIX = "[ComprehensiveAnimationAPITest]"

class ComprehensiveAPITester:
    def __init__(self):
        self.ctx = zmq.Context()
        self.push = self.ctx.socket(zmq.PUSH)
        self.push.connect('ipc:///tmp/doly_control.sock')
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
            return False
    
    def test_drive_distance_pid(self):
        """测试 drive_distance_pid PID 版本"""
        print(f"\n{LOG_PREFIX} ═════════════════════════════════════════")
        print(f"{LOG_PREFIX} 测试 A: drive_distance_pid (PID 精确距离移动)")
        print(f"{LOG_PREFIX} ═════════════════════════════════════════")
        
        tests = [
            ("前进 50mm (PID)", {
                "distance_mm": 50,
                "speed": 15,
                "accel": 0,
                "brake": 0,
                "direction": 0
            }),
            ("前进 100mm (PID) 速度 20%", {
                "distance_mm": 100,
                "speed": 20,
                "accel": 0,
                "brake": 0,
                "direction": 0
            }),
            ("后退 60mm (PID)", {
                "distance_mm": 60,
                "speed": 20,
                "accel": 0,
                "brake": 0,
                "direction": 1
            }),
        ]
        
        for name, params in tests:
            self.send_command("move_distance_cm_pid", params, name)
            time.sleep(2.5)
        
        print(f"{LOG_PREFIX} ✓ move_distance_cm_pid 测试完成")
    
    def test_turn_deg_pid_advanced(self):
        """测试 turn_deg_pid PID 转向版本"""
        print(f"\n{LOG_PREFIX} ═════════════════════════════════════════")
        print(f"{LOG_PREFIX} 测试 B: turn_deg_pid (PID 精确转向)")
        print(f"{LOG_PREFIX} ═════════════════════════════════════════")
        
        tests = [
            ("PID 原地左转 45°", {
                "angle_deg": 45,
                "speed": 20,
                "is_center": True
            }),
            ("PID 原地右转 45°", {
                "angle_deg": -45,
                "speed": 20,
                "is_center": True
            }),
            ("PID 原地左转 90°", {
                "angle_deg": 90,
                "speed": 20,
                "is_center": True
            }),
            ("PID 轮轴心左转 45°", {
                "angle_deg": 45,
                "speed": 20,
                "is_center": False
            }),
        ]
        
        for name, params in tests:
            self.send_command("turn_deg_pid", params, name)
            time.sleep(2.5)
        
        print(f"{LOG_PREFIX} ✓ turn_deg_pid 测试完成")
    
    def test_profile_vs_pid_comparison(self):
        """比较 Profile 模式 (drive_distance) vs PID 模式 (drive_distance_pid)"""
        print(f"\n{LOG_PREFIX} ═════════════════════════════════════════")
        print(f"{LOG_PREFIX} 测试 C: Profile 模式 vs PID 模式对比")
        print(f"{LOG_PREFIX} ═════════════════════════════════════════")
        
        # Profile 模式：前进 100mm (可能有加速度阶段)
        print(f"{LOG_PREFIX} [C1] Profile 模式：前进 100mm")
        self.send_command("move_distance_cm_pid", {
            "distance_mm": 100,
            "speed": 20,
            "accel": 20,
            "brake": 20,
            "direction": 0
        }, "Profile 前进 100mm (加速 20%, 刹车 20%)")
        time.sleep(2.5)
        
        # PID 模式：前进 100mm (更精确的反馈控制)
        print(f"{LOG_PREFIX} [C2] PID 模式：前进 100mm")
        self.send_command("move_distance_cm_pid", {
            "distance_mm": 100,
            "speed": 20,
            "accel": 0,
            "brake": 0,
            "direction": 0
        }, "PID 前进 100mm (精确反馈控制)")
        time.sleep(2.5)
        
        print(f"{LOG_PREFIX} ✓ Profile vs PID 对比测试完成")
    
    def test_combined_sequence_pid(self):
        """测试 PID 版本的组合序列"""
        print(f"\n{LOG_PREFIX} ═════════════════════════════════════════")
        print(f"{LOG_PREFIX} 测试 D: PID 版本组合动作序列")
        print(f"{LOG_PREFIX} ═════════════════════════════════════════")
        
        sequence = [
            ("PID 前进 80mm", {
                "distance_mm": 80,
                "speed": 20,
                "accel": 0,
                "brake": 0,
                "direction": 0
            }, "move_distance_cm_pid"),
            ("PID 原地左转 90°", {
                "angle_deg": 90,
                "speed": 20,
                "is_center": True
            }, "turn_deg_pid"),
            ("PID 前进 50mm", {
                "distance_mm": 50,
                "speed": 20,
                "accel": 0,
                "brake": 0,
                "direction": 0
            }, "drive_distance_pid"),
            ("PID 原地右转 90°", {
                "angle_deg": -90,
                "speed": 20,
                "is_center": True
            }, "turn_deg_pid"),
        ]
        
        for name, params, action in sequence:
            self.send_command(action, params, name)
            time.sleep(2.5)
        
        print(f"{LOG_PREFIX} ✓ PID 组合动作序列完成")
    
    def test_parameter_compatibility_pid(self):
        """测试 PID 版本的参数兼容性"""
        print(f"\n{LOG_PREFIX} ═════════════════════════════════════════")
        print(f"{LOG_PREFIX} 测试 E: PID 版本参数兼容性")
        print(f"{LOG_PREFIX} ═════════════════════════════════════════")
        
        tests = [
            ("drive_distance_pid 使用 'distance' 字段", {
                "distance": 60,  # 而非 distance_mm
                "speed": 20,
                "accel": 0,
                "brake": 0,
                "direction": 0
            }, "drive_distance_pid"),
            ("turn_deg_pid 使用 'driveRotate' 字段", {
                "driveRotate": 45,  # 而非 angle_deg
                "speed": 20,
                "isCenter": True  # 而非 is_center
            }, "turn_deg_pid"),
        ]
        
        for name, params, action in tests:
            self.send_command(action, params, name)
            time.sleep(2.5)
        
        print(f"{LOG_PREFIX} ✓ PID 参数兼容性测试完成")
    
    def run_all_tests(self):
        """运行所有测试"""
        print(f"\n{LOG_PREFIX} ╔════════════════════════════════════════════╗")
        print(f"{LOG_PREFIX} ║   完整 Motor API 测试（含 PID 版本）       ║")
        print(f"{LOG_PREFIX} ║   Start Time: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}          ║")
        print(f"{LOG_PREFIX} ╚════════════════════════════════════════════╝")
        
        try:
            self.test_drive_distance_pid()
            self.test_turn_deg_pid_advanced()
            self.test_profile_vs_pid_comparison()
            self.test_combined_sequence_pid()
            self.test_parameter_compatibility_pid()
            
            print(f"\n{LOG_PREFIX} ╔════════════════════════════════════════════╗")
            print(f"{LOG_PREFIX} ║   ✓ 所有测试完成                           ║")
            print(f"{LOG_PREFIX} ║   End Time: {datetime.now().strftime('%Y-%m-%d %H:%M:%S')}          ║")
            print(f"{LOG_PREFIX} ║   测试项目：                                ║")
            print(f"{LOG_PREFIX} ║   ✓ A. drive_distance_pid (PID 距离)       ║")
            print(f"{LOG_PREFIX} ║   ✓ B. turn_deg_pid (PID 转向)    ║")
            print(f"{LOG_PREFIX} ║   ✓ C. Profile vs PID 对比                 ║")
            print(f"{LOG_PREFIX} ║   ✓ D. PID 组合序列                        ║")
            print(f"{LOG_PREFIX} ║   ✓ E. 参数兼容性                          ║")
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
    tester = ComprehensiveAPITester()
    tester.run_all_tests()


if __name__ == "__main__":
    main()
