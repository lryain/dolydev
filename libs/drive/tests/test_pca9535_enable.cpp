/**
 * @file test_pca9535_enable.cpp
 * @brief 测试硬件使能控制功能
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/pca9535_service.hpp"
#include <iostream>
#include <csignal>
#include <chrono>
#include <thread>

using namespace doly::extio;

static bool g_running = true;

void signal_handler(int) {
    std::cout << "\n[信号] 收到退出信号" << std::endl;
    g_running = false;
}

int main() {
    // 注册信号处理
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    std::cout << "=== PCA9535 硬件使能控制测试 ===" << std::endl;

    // 创建配置（默认启用悬崖传感器）
    Pca9535ConfigV2 config;
    config.enable_cliff_default = true;
    config.enable_servo_left_default = false;
    config.enable_servo_right_default = false;
    config.enable_tof_default = false;

    // 初始化服务
    Pca9535Service service;
    if (!service.init(&config)) {
        std::cerr << "[错误] 服务初始化失败" << std::endl;
        return 1;
    }

    if (!service.start()) {
        std::cerr << "[错误] 服务启动失败" << std::endl;
        return 1;
    }

    std::cout << "\n[提示] 服务已启动，开始测试硬件使能控制..." << std::endl;
    std::cout << "[提示] 按 Ctrl+C 退出\n" << std::endl;

    // 测试序列
    int test_cycle = 0;
    while (g_running) {
        test_cycle++;
        std::cout << "\n--- 测试周期 " << test_cycle << " ---" << std::endl;

        // 1. 测试左舵机
        std::cout << "🔧 启用左舵机..." << std::endl;
        service.enable_servo_left(true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        std::cout << "🔧 禁用左舵机..." << std::endl;
        service.enable_servo_left(false);
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 2. 测试右舵机
        std::cout << "🔧 启用右舵机..." << std::endl;
        service.enable_servo_right(true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        std::cout << "🔧 禁用右舵机..." << std::endl;
        service.enable_servo_right(false);
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 3. 测试 TOF
        std::cout << "📏 启用 TOF 传感器..." << std::endl;
        service.enable_tof(true);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        std::cout << "📏 禁用 TOF 传感器..." << std::endl;
        service.enable_tof(false);
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 4. 测试悬崖传感器（切换）
        std::cout << "⚠️  禁用悬崖传感器..." << std::endl;
        service.enable_cliff(false);
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        std::cout << "⚠️  启用悬崖传感器..." << std::endl;
        service.enable_cliff(true);
        std::this_thread::sleep_for(std::chrono::seconds(1));

        // 5. 测试扩展 IO
        std::cout << "🔌 测试扩展 IO 0-5..." << std::endl;
        for (uint8_t i = 0; i < 6; i++) {
            std::cout << "  EXT_IO_" << (int)i << " = HIGH" << std::endl;
            service.set_ext_io(i, true);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            
            std::cout << "  EXT_IO_" << (int)i << " = LOW" << std::endl;
            service.set_ext_io(i, false);
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
        }

        // 打印统计信息
        auto stats = service.get_stats();
        std::cout << "\n[统计] IRQ: " << stats.irq_count
                  << ", 引脚事件: " << stats.pin_events
                  << ", 触摸手势: " << stats.touch_gestures
                  << ", 悬崖模式: " << stats.cliff_patterns << std::endl;

        std::this_thread::sleep_for(std::chrono::seconds(3));
    }

    std::cout << "\n[退出] 正在停止服务..." << std::endl;
    service.stop();
    
    std::cout << "[退出] 测试完成" << std::endl;
    return 0;
}
