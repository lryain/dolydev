#!/usr/bin/env python3
"""
动画系统电机 API 完整测试
测试所有动画系统需要的电机控制功能
"""
import zmq
import json
import time
import sys

class AnimationMotorTester:
    def __init__(self):
        self.ctx = zmq.Context()
        self.push = self.ctx.socket(zmq.PUSH)
        self.push.connect('ipc:///tmp/doly_control.sock')
        print("✅ 连接到 ipc:///tmp/doly_control.sock")
        time.sleep(0.5)  # 等待连接建立
    
    def send_command(self, action, params, desc=""):
        """发送命令到 drive_service"""
        cmd = {"action": action}
        cmd.update(params)
        
        self.push.send_string("io.pca9535.control", zmq.SNDMORE)
        self.push.send_string(json.dumps(cmd))
        
        if desc:
            print(f"\n{'='*60}")
            print(f"🎯 {desc}")
            print(f"   Action: {action}")
            print(f"   Params: {json.dumps(params, indent=2)}")
        else:
            print(f"✅ 发送: {action} {params}")
        
        time.sleep(0.1)  # 短暂延迟，让命令发送完成
    
    def wait_and_check(self, seconds, message=""):
        """等待并提示"""
        if message:
            print(f"⏳ {message} ({seconds}秒)")
        time.sleep(seconds)
    
    def test_basic_movement(self):
        """测试1: 基础移动 - 前进/后退"""
        print("\n" + "="*60)
        print("📦 测试1: 基础移动控制")
        print("="*60)
        
        # 1.1 前进固定时长
        self.send_command("motor_forward", 
                         {"speed": 0.3, "duration": 1.5},
                         "前进 1.5秒 (速度30%)")
        self.wait_and_check(2.0, "等待前进完成")
        
        # 1.2 后退固定时长
        self.send_command("motor_backward",
                         {"speed": 0.3, "duration": 1.5},
                         "后退 1.5秒 (速度30%)")
        self.wait_and_check(2.0, "等待后退完成")
        
        # 1.3 停止
        self.send_command("motor_stop", {}, "停止电机")
        self.wait_and_check(1.0)
    
    def test_precise_distance(self):
        """测试2: 精确距离控制 - 对应 drive_distance block"""
        print("\n" + "="*60)
        print("📏 测试2: 精确距离控制 (drive_distance)")
        print("="*60)
        
        # 2.1 前进 10cm
        self.send_command("move_distance_cm",
                         {
                             "distance_cm": 10.0,   # 10cm
                             "throttle": 0.35,      # 速度 25%
                             "timeout_ms": 5000     # 5秒超时
                         },
                         "前进 10cm (对应 distance=100mm, speed=25)")
        self.wait_and_check(4.0, "等待移动完成")
        
        # 2.2 后退 10cm
        self.send_command("move_distance_cm",
                         {
                             "distance_cm": -10.0,  # 后退 10cm
                             "throttle": 0.35,
                             "timeout_ms": 5000
                         },
                         "后退 10cm (distance=-100mm)")
        self.wait_and_check(4.0, "等待移动完成")
        
        # 2.3 前进 5cm (更精确的短距离)
        self.send_command("move_distance_cm",
                         {
                             "distance_cm": 5.0,
                             "throttle": 0.3,       # 更慢速度
                             "timeout_ms": 3000
                         },
                         "前进 5cm (distance=50mm, speed=20)")
        self.wait_and_check(3.0, "等待移动完成")
        
        self.send_command("motor_stop", {}, "停止")
        self.wait_and_check(1.0)
    
    def test_rotation(self):
        """测试3: 精确转向控制 - 对应 drive_rotate_left block"""
        print("\n" + "="*60)
        print("🔄 测试3: 精确转向控制 (drive_rotate_left)")
        print("="*60)
        
        # 3.1 右转 90度 (正角度)
        self.send_command("turn_deg",
                         {
                             "angle_deg": 35.0,     # 正值=右转
                             "throttle": 0.35,      # 转速 25%
                             "isCenter": False,
                             "timeout_ms": 5000
                         },
                         "右转 35° (driveRotate=35, isCenter=TRUE)")
        self.wait_and_check(1.0, "等待转向完成")
        
        # 3.2 左转 35度 (负角度)
        self.send_command("turn_deg",
                         {
                             "angle_deg": -35.0,    # 负值=左转
                             "throttle": 0.35,
                             "isCenter": False,
                             "timeout_ms": 5000
                         },
                         "左转 35° (driveRotate=-35, isCenter=TRUE)")
        self.wait_and_check(1.0, "等待转向完成")
        
        # 3.3 右转 45度 (小角度)
        # self.send_command("turn_deg",
        #                  {
        #                      "angle_deg": 15.0,
        #                      "throttle": 0.35,
        #                      "timeout_ms": 3000
        #                  },
        #                  "右转 45° (driveRotate=45)")
        # self.wait_and_check(1.0, "等待转向完成")
        
        # # 3.4 左转 45度 (恢复)
        # self.send_command("turn_deg",
        #                  {
        #                      "angle_deg": -15.0,
        #                      "throttle": 0.35,
        #                      "timeout_ms": 3000
        #                  },
        #                  "左转 45° (恢复)")
        # self.wait_and_check(1.0, "等待转向完成")
        
        self.send_command("motor_stop", {}, "停止")
        self.wait_and_check(1.0)
    
    def test_manual_turning(self):
        """测试4: 手动转向 - 时长控制"""
        print("\n" + "="*60)
        print("🔃 测试4: 手动转向控制 (时长模式)")
        print("="*60)
        
        # 4.1 左转 1秒
        self.send_command("motor_turn_left",
                         {"speed": 0.3, "duration": 1.0},
                         "左转 1秒 (速度30%)")
        self.wait_and_check(1.5, "等待左转完成")
        
        # 4.2 右转 1秒 (恢复)
        self.send_command("motor_turn_right",
                         {"speed": 0.3, "duration": 1.0},
                         "右转 1秒 (恢复)")
        self.wait_and_check(1.5, "等待右转完成")
        
        self.send_command("motor_stop", {}, "停止")
        self.wait_and_check(1.0)
    
    def test_speed_control(self):
        """测试5: 手动速度控制"""
        print("\n" + "="*60)
        print("⚡ 测试5: 手动速度控制 (左右轮独立)")
        print("="*60)
        
        # 5.1 双轮同速前进
        self.send_command("set_motor_speed",
                         {"left": 0.3, "right": 0.3, "duration": 1.0},
                         "双轮同速前进 (L=0.3, R=0.3, 1秒)")
        self.wait_and_check(1.5, "等待前进完成")
        
        # 5.2 左轮快，右轮慢 (右转弧线)
        self.send_command("set_motor_speed",
                         {"left": 0.4, "right": 0.2, "duration": 1.0},
                         "右转弧线 (L=0.4, R=0.2, 1秒)")
        self.wait_and_check(1.5, "等待转向完成")
        
        # 5.3 左轮慢，右轮快 (左转弧线)
        self.send_command("set_motor_speed",
                         {"left": 0.2, "right": 0.4, "duration": 1.0},
                         "左转弧线 (L=0.2, R=0.4, 1秒)")
        self.wait_and_check(1.5, "等待转向完成")
        
        # 5.4 反向旋转 (原地转)
        self.send_command("set_motor_speed",
                         {"left": -0.3, "right": 0.3, "duration": 1.0},
                         "原地左转 (L=-0.3, R=0.3, 1秒)")
        self.wait_and_check(1.5, "等待转向完成")
        
        self.send_command("motor_stop", {}, "停止")
        self.wait_and_check(1.0)
    
    def test_encoder_queries(self):
        """测试6: 编码器查询"""
        print("\n" + "="*60)
        print("📊 测试6: 编码器状态查询")
        print("="*60)
        
        # 6.1 查询当前编码器值
        self.send_command("get_encoder_values", {},
                         "查询编码器当前值")
        self.wait_and_check(0.5, "等待查询结果")
        
        # 6.2 移动一段距离后再查询
        self.send_command("move_distance_cm",
                         {"distance_cm": 10.0, "throttle": 0.3, "timeout_ms": 5000},
                         "前进 10cm")
        self.wait_and_check(4.0, "等待移动完成")
        
        self.send_command("get_encoder_values", {},
                         "查询移动后的编码器值")
        self.wait_and_check(0.5, "等待查询结果")
        
        self.send_command("motor_stop", {}, "停止")
        self.wait_and_check(1.0)
    
    def test_pulse_control(self):
        """测试7: 脉冲级控制 (底层API)"""
        print("\n" + "="*60)
        print("⚙️  测试7: 编码器脉冲控制 (底层API)")
        print("="*60)
        
        # 7.1 前进 100 脉冲
        self.send_command("motor_move_pulses",
                         {
                             "pulses": 100,         # 脉冲数
                             "throttle": 0.35,
                             "assume_rate": 100.0,  # 假设脉冲率
                             "timeout": 3.0
                         },
                         "前进 100 脉冲 (~7.85cm)")
        self.wait_and_check(4.0, "等待移动完成")
        
        # 7.2 后退 100 脉冲
        self.send_command("motor_move_pulses",
                         {
                             "pulses": -100,
                             "throttle": 0.35,
                             "assume_rate": 100.0,
                             "timeout": 3.0
                         },
                         "后退 100 脉冲 (~7.85cm)")
        self.wait_and_check(4.0, "等待移动完成")
        
        self.send_command("motor_stop", {}, "停止")
        self.wait_and_check(1.0)
    
    def test_animation_scenario(self):
        """测试8: 模拟动画场景 - 复合动作"""
        print("\n" + "="*60)
        print("🎬 测试8: 动画场景模拟 (go_forward + go_left)")
        print("="*60)
        
        print("\n场景1: 前进动画 (go_forward.xml)")
        print("-" * 40)
        
        # 对应 go_forward.xml 中的 drive_distance
        self.send_command("move_distance_cm",
                         {
                             "distance_cm": 10.0,   # distance=100mm
                             "throttle": 0.3,       # speed=20 -> 0.2
                             "timeout_ms": 5000
                         },
                         "执行 go_forward: 前进 10cm (speed=20)")
        self.wait_and_check(4.0, "等待前进完成")
        
        print("\n场景2: 左转动画 (go_left.xml)")
        print("-" * 40)
        
        # 对应 go_left.xml 中的 drive_rotate_left
        self.send_command("turn_deg",
                         {
                             "angle_deg": -90.0,    # driveRotate=90, 左转用负值
                             "throttle": 0.3,       # speed=20 -> 0.2
                             "timeout_ms": 5000
                         },
                         "执行 go_left: 左转 90° (speed=20, isCenter=TRUE)")
        self.wait_and_check(4.0, "等待转向完成")
        
        print("\n场景3: 前进 + 右转组合")
        print("-" * 40)
        
        # 前进
        self.send_command("move_distance_cm",
                         {"distance_cm": 5.0, "throttle": 0.35, "timeout_ms": 3000},
                         "前进 5cm")
        self.wait_and_check(10.0)
        
        # 右转
        self.send_command("turn_deg",
                         {"angle_deg": 45.0, "throttle": 0.35, "timeout_ms": 3000},
                         "右转 45°")
        self.wait_and_check(10.0)
        
        # 再前进
        self.send_command("move_distance_cm",
                         {"distance_cm": 5.0, "throttle": 0.35, "timeout_ms": 3000},
                         "再前进 5cm")
        self.wait_and_check(3.0)
        
        self.send_command("motor_stop", {}, "停止")
        self.wait_and_check(1.0)
    
    def run_all_tests(self):
        """运行所有测试"""
        print("\n" + "="*60)
        print("🚀 动画系统电机 API 完整测试")
        print("="*60)
        print("测试内容:")
        print("  1. 基础移动控制 (前进/后退)")
        print("  2. 精确距离控制 (drive_distance)")
        print("  3. 精确转向控制 (drive_rotate_left)")
        print("  4. 手动转向控制 (时长模式)")
        print("  5. 手动速度控制 (左右轮独立)")
        print("  6. 编码器状态查询")
        print("  7. 编码器脉冲控制 (底层API)")
        print("  8. 动画场景模拟 (复合动作)")
        print("="*60)
        
        print("\n按回车键开始测试...")
        
        try:
            # 测试1: 基础移动
            self.test_basic_movement()
            
            # 测试2: 精确距离
            self.test_precise_distance()
            
            # 测试3: 精确转向
            self.test_rotation()
            
            # 测试4: 手动转向
            self.test_manual_turning()
            
            # 测试5: 速度控制
            self.test_speed_control()
            
            # 测试6: 编码器查询
            self.test_encoder_queries()
            
            # 测试7: 脉冲控制
            self.test_pulse_control()
            
            # 测试8: 动画场景
            self.test_animation_scenario()
            
            print("\n" + "="*60)
            print("✅ 所有测试完成！")
            print("="*60)
            
        except KeyboardInterrupt:
            print("\n\n❌ 测试被用户中断")
            self.send_command("motor_stop", {}, "紧急停止")
        except Exception as e:
            print(f"\n\n❌ 测试出错: {e}")
            import traceback
            traceback.print_exc()
            self.send_command("motor_stop", {}, "紧急停止")
    
    def close(self):
        """清理资源"""
        self.push.close()
        self.ctx.term()

def main():
    print("""
╔══════════════════════════════════════════════════════════════╗
║         动画系统电机 API 完整测试程序                        ║
║                                                              ║
║  测试前请确保:                                               ║
║    1. drive_service 已启动                                   ║
║    2. 电机已连接并正常工作                                    ║
║    3. Doly 放在安全的测试区域                                ║
║    4. 准备好观察电机运动和日志输出                           ║
╚══════════════════════════════════════════════════════════════╝
    """)
    
    # 检查参数
    if len(sys.argv) > 1:
        test_name = sys.argv[1]
        tester = AnimationMotorTester()
        
        if test_name == "1" or test_name == "basic":
            tester.test_basic_movement()
        elif test_name == "2" or test_name == "distance":
            tester.test_precise_distance()
        elif test_name == "3" or test_name == "rotate":
            tester.test_rotation()
        elif test_name == "4" or test_name == "turn":
            tester.test_manual_turning()
        elif test_name == "5" or test_name == "speed":
            tester.test_speed_control()
        elif test_name == "6" or test_name == "encoder":
            tester.test_encoder_queries()
        elif test_name == "7" or test_name == "pulse":
            tester.test_pulse_control()
        elif test_name == "8" or test_name == "animation":
            tester.test_animation_scenario()
        else:
            print(f"❌ 未知测试: {test_name}")
            print("用法: python3 test_animation_motor_api.py [1-8|basic|distance|rotate|turn|speed|encoder|pulse|animation]")
            sys.exit(1)
        
        tester.close()
    else:
        # 运行所有测试
        tester = AnimationMotorTester()
        tester.run_all_tests()
        tester.close()

if __name__ == "__main__":
    try:
        main()
    except KeyboardInterrupt:
        print("\n\n❌ 程序被用户中断")
    except Exception as e:
        print(f"\n\n❌ 程序错误: {e}")
        import traceback
        traceback.print_exc()
        sys.exit(1)
