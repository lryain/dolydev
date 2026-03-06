/**
 * @file test_pca9535.cpp
 * @brief PCA9535 完整功能测试
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/pca9535_service.hpp"
#include <iostream>
#include <csignal>
#include <atomic>
#include <thread>
#include <chrono>

using namespace doly::extio;

std::atomic<bool> g_running{true};

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        std::cout << "\n收到退出信号" << std::endl;
        g_running.store(false);
    }
}

const char* pin_name(Pca9535Pin pin) {
    switch (pin) {
        case Pca9535Pin::IRS_FL: return "IRS_FL";
        case Pca9535Pin::IRS_FR: return "IRS_FR";
        case Pca9535Pin::IRS_BL: return "IRS_BL";
        case Pca9535Pin::IRS_BR: return "IRS_BR";
        case Pca9535Pin::TOUCH_R: return "TOUCH_R";
        case Pca9535Pin::TOUCH_L: return "TOUCH_L";
        case Pca9535Pin::SRV_L_EN: return "SRV_L_EN";
        case Pca9535Pin::SRV_R_EN: return "SRV_R_EN";
        case Pca9535Pin::TOF_ENL: return "TOF_ENL";
        case Pca9535Pin::IRS_DRV: return "IRS_DRV";
        default: return "UNKNOWN";
    }
}

const char* gesture_name(TouchGesture gesture) {
    switch (gesture) {
        case TouchGesture::SingleTap: return "SingleTap";
        case TouchGesture::DoubleTap: return "DoubleTap";
        case TouchGesture::LongPress: return "LongPress";
        default: return "None";
    }
}

const char* pattern_name(CliffPattern pattern) {
    switch (pattern) {
        case CliffPattern::StableFloor: return "StableFloor";
        case CliffPattern::CliffDetected: return "CliffDetected";
        case CliffPattern::BlackWhiteLine: return "BlackWhiteLine";
        case CliffPattern::Noisy: return "Noisy";
        default: return "Unknown";
    }
}

int main() {
    std::cout << "╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║  PCA9535 扩展 IO 完整功能测试          ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝\n" << std::endl;

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 创建服务
    Pca9535Service service;

    // 初始化
    std::cout << "初始化 PCA9535 服务..." << std::endl;
    if (!service.init()) {
        std::cerr << "❌ 初始化失败" << std::endl;
        return 1;
    }
    std::cout << "✅ 初始化成功\n" << std::endl;

    // 订阅所有输入引脚的原始事件
    std::cout << "订阅原始引脚事件..." << std::endl;
    service.subscribe_pin(Pca9535Pin::IRS_FL, [](const PinChangeEvent& e) {
        std::cout << "[PIN] " << pin_name(e.pin) 
                  << " = " << (e.value ? "HIGH" : "LOW") << std::endl;
    });
    service.subscribe_pin(Pca9535Pin::IRS_FR, [](const PinChangeEvent& e) {
        std::cout << "[PIN] " << pin_name(e.pin) 
                  << " = " << (e.value ? "HIGH" : "LOW") << std::endl;
    });
    service.subscribe_pin(Pca9535Pin::IRS_BL, [](const PinChangeEvent& e) {
        std::cout << "[PIN] " << pin_name(e.pin) 
                  << " = " << (e.value ? "HIGH" : "LOW") << std::endl;
    });
    service.subscribe_pin(Pca9535Pin::IRS_BR, [](const PinChangeEvent& e) {
        std::cout << "[PIN] " << pin_name(e.pin) 
                  << " = " << (e.value ? "HIGH" : "LOW") << std::endl;
    });
    service.subscribe_pin(Pca9535Pin::TOUCH_L, [](const PinChangeEvent& e) {
        std::cout << "[PIN] " << pin_name(e.pin) 
                  << " = " << (e.value ? "HIGH" : "LOW") << std::endl;
    });
    service.subscribe_pin(Pca9535Pin::TOUCH_R, [](const PinChangeEvent& e) {
        std::cout << "[PIN] " << pin_name(e.pin) 
                  << " = " << (e.value ? "HIGH" : "LOW") << std::endl;
    });

    // 订阅触摸手势
    std::cout << "订阅触摸手势..." << std::endl;
    service.subscribe_touch(Pca9535Pin::TOUCH_L, [](const TouchGestureEvent& e) {
        std::cout << "🖐️  [GESTURE] " << pin_name(e.pin) 
                  << " - " << gesture_name(e.gesture) << std::endl;
    });
    service.subscribe_touch(Pca9535Pin::TOUCH_R, [](const TouchGestureEvent& e) {
        std::cout << "🖐️  [GESTURE] " << pin_name(e.pin) 
                  << " - " << gesture_name(e.gesture) << std::endl;
    });

    // 订阅悬崖模式
    std::cout << "订阅悬崖模式..." << std::endl;
    service.subscribe_cliff(Pca9535Pin::IRS_FL, [](const CliffPatternEvent& e) {
        std::cout << "⚠️  [CLIFF] " << pin_name(e.pin) 
                  << " - " << pattern_name(e.pattern) << std::endl;
    });
    service.subscribe_cliff(Pca9535Pin::IRS_FR, [](const CliffPatternEvent& e) {
        std::cout << "⚠️  [CLIFF] " << pin_name(e.pin) 
                  << " - " << pattern_name(e.pattern) << std::endl;
    });
    service.subscribe_cliff(Pca9535Pin::IRS_BL, [](const CliffPatternEvent& e) {
        std::cout << "⚠️  [CLIFF] " << pin_name(e.pin) 
                  << " - " << pattern_name(e.pattern) << std::endl;
    });
    service.subscribe_cliff(Pca9535Pin::IRS_BR, [](const CliffPatternEvent& e) {
        std::cout << "⚠️  [CLIFF] " << pin_name(e.pin) 
                  << " - " << pattern_name(e.pattern) << std::endl;
    });

    std::cout << "✅ 订阅完成\n" << std::endl;

    // 启动服务
    std::cout << "启动 IRQ 监控线程..." << std::endl;
    if (!service.start()) {
        std::cerr << "❌ 启动失败" << std::endl;
        return 1;
    }
    std::cout << "✅ 启动成功\n" << std::endl;

    // 设置输出引脚（示例）
    std::cout << "设置输出引脚..." << std::endl;
    service.set_output(Pca9535Pin::IRS_DRV, true);   // 使能悬崖 IR
    service.set_output(Pca9535Pin::TOF_ENL, true);   // 使能 TOF
    std::cout << "✅ 输出设置完成\n" << std::endl;

    std::cout << "========================================" << std::endl;
    std::cout << "监控中... （按 Ctrl+C 退出）" << std::endl;
    std::cout << "========================================\n" << std::endl;

    // 主循环：定期打印统计信息
    while (g_running.load()) {
        std::this_thread::sleep_for(std::chrono::seconds(5));

        if (!g_running.load()) break;

        auto stats = service.get_stats();
        std::cout << "\n[统计] IRQ: " << stats.irq_count 
                  << ", 引脚事件: " << stats.pin_events
                  << ", 触摸手势: " << stats.touch_gestures
                  << ", 悬崖模式: " << stats.cliff_patterns << std::endl;
    }

    // 停止服务
    std::cout << "\n停止服务..." << std::endl;
    service.stop();

    std::cout << "\n╔════════════════════════════════════════╗" << std::endl;
    std::cout << "║  测试完成                              ║" << std::endl;
    std::cout << "╚════════════════════════════════════════╝" << std::endl;

    return 0;
}
