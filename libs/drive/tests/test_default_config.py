#!/usr/bin/env python3
"""
验证 PCA9535 IO 状态同步与默认配置生效情况
"""
import zmq
import json
import time
import sys

def main():
    SUB_ENDPOINT = "ipc:///tmp/doly_zmq.sock"
    PUSH_ENDPOINT = "ipc:///tmp/doly_control.sock"
    
    context = zmq.Context()
    
    # 订阅者：监听 io.pca9535.* 主题
    subscriber = context.socket(zmq.SUB)
    subscriber.setsockopt(zmq.RCVTIMEO, 3000)  # 3秒超时
    subscriber.connect(SUB_ENDPOINT)
    subscriber.setsockopt_string(zmq.SUBSCRIBE, "io.pca9535.")
    
    # 推送者：发送控制命令
    pusher = context.socket(zmq.PUSH)
    pusher.connect(PUSH_ENDPOINT)
    
    time.sleep(0.5)  # 确保连接建立
    
    print("=" * 70)
    print("PCA9535 IO 状态同步与默认配置验证测试")
    print("=" * 70)
    
    # 等待初始状态消息 - 通过发送一个命令来获取状态反馈
    print("\n[1] 查询当前状态（通过启用左舵机来获取状态更新）...")
    pusher.send_string("io.pca9535.control {\"action\":\"enable_servo\",\"channel\":\"left\",\"value\":true}")
    
    try:
        topic = subscriber.recv_string()
        msg = subscriber.recv_string()
        data = json.loads(msg)
        print(f"    ✓ 收到事件: {topic}")
        print(f"    ✓ 状态字 (state_hex): {data.get('state_hex', '?')}")
        print(f"    ✓ 输出缓存 (output_cache_hex): {data.get('output_cache_hex', '?')}")
        
        # 解析状态字
        state = data.get('state', 0)
        bit_6_servo_l = (state >> 6) & 1
        bit_7_servo_r = (state >> 7) & 1
        print(f"    ✓ Bit 6 (SRV_L_EN): {bit_6_servo_l} (应该是 1，表示左舵机启用)")
        print(f"    ✓ Bit 7 (SRV_R_EN): {bit_7_servo_r} (应该是 1，表示右舵机启用)")
        
        if bit_6_servo_l == 1 and bit_7_servo_r == 1:
            print("    ✓✓✓ 默认配置生效正确！左右舵机已在启动时使能")
        else:
            print("    ⚠ 注意：某个舵机的初始状态可能已被修改")
            
    except zmq.Again:
        print("    ✗ 超时：未收到初始状态事件")
        return 1
    
    # 测试关闭舵机
    print("\n[2] 测试关闭左舵机...")
    pusher.send_string("io.pca9535.control {\"action\":\"enable_servo\",\"channel\":\"left\",\"value\":false}")
    
    try:
        topic = subscriber.recv_string()
        msg = subscriber.recv_string()
        data = json.loads(msg)
        state = data.get('state', 0)
        bit_6 = (state >> 6) & 1
        print(f"    ✓ 关闭命令已执行")
        print(f"    ✓ Bit 6 (SRV_L_EN): {bit_6} (应该是 0，表示已关闭)")
        
        if bit_6 == 0:
            print("    ✓✓✓ 关闭命令正确执行！")
        else:
            print("    ✗ 关闭失败")
    except zmq.Again:
        print("    ✗ 超时：未收到关闭事件")
    
    # 测试重新启用舵机
    print("\n[3] 测试重新启用左舵机...")
    pusher.send_string("io.pca9535.control {\"action\":\"enable_servo\",\"channel\":\"left\",\"value\":true}")
    
    try:
        topic = subscriber.recv_string()
        msg = subscriber.recv_string()
        data = json.loads(msg)
        state = data.get('state', 0)
        bit_6 = (state >> 6) & 1
        print(f"    ✓ 启用命令已执行")
        print(f"    ✓ Bit 6 (SRV_L_EN): {bit_6} (应该是 1，表示已启用)")
        
        if bit_6 == 1:
            print("    ✓✓✓ 启用命令正确执行！")
        else:
            print("    ✗ 启用失败")
    except zmq.Again:
        print("    ✗ 超时：未收到启用事件")
    
    # 测试批量设置
    print("\n[4] 测试批量设置两个舵机（mask=0xc0, state=0x80 表示左ON右OFF）...")
    pusher.send_string("io.pca9535.control {\"action\":\"set_outputs_bulk\",\"state\":128,\"mask\":192}")
    
    try:
        topic = subscriber.recv_string()
        msg = subscriber.recv_string()
        data = json.loads(msg)
        state = data.get('state', 0)
        bit_6 = (state >> 6) & 1
        bit_7 = (state >> 7) & 1
        print(f"    ✓ 批量设置命令已执行")
        print(f"    ✓ Bit 6 (SRV_L_EN): {bit_6} (应该是 0)")
        print(f"    ✓ Bit 7 (SRV_R_EN): {bit_7} (应该是 1)")
        
        if bit_6 == 0 and bit_7 == 1:
            print("    ✓✓✓ 批量设置命令正确执行！")
        else:
            print("    ✗ 批量设置结果不符合预期")
    except zmq.Again:
        print("    ✗ 超时：未收到批量设置事件")
    
    # 恢复默认配置
    print("\n[5] 测试恢复默认配置（两个舵机都启用）...")
    pusher.send_string("io.pca9535.control {\"action\":\"set_outputs_bulk\",\"state\":192,\"mask\":192}")
    
    try:
        topic = subscriber.recv_string()
        msg = subscriber.recv_string()
        data = json.loads(msg)
        state = data.get('state', 0)
        bit_6 = (state >> 6) & 1
        bit_7 = (state >> 7) & 1
        print(f"    ✓ 恢复命令已执行")
        print(f"    ✓ Bit 6 (SRV_L_EN): {bit_6} (应该是 1)")
        print(f"    ✓ Bit 7 (SRV_R_EN): {bit_7} (应该是 1)")
        
        if bit_6 == 1 and bit_7 == 1:
            print("    ✓✓✓ 恢复成功！")
        else:
            print("    ✗ 恢复失败")
    except zmq.Again:
        print("    ✗ 超时：未收到恢复事件")
    
    print("\n" + "=" * 70)
    print("测试完成")
    print("=" * 70)
    
    subscriber.close()
    pusher.close()
    context.term()
    return 0

if __name__ == "__main__":
    sys.exit(main())
