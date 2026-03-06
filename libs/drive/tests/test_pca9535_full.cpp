/**
 * @file test_pca9535_full.cpp
 * @brief PCA9535 完整功能测试（YAML配置 + 历史数据 + 消息发布）
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/pca9535_service.hpp"
#include "doly/pca9535_config_v2.hpp"
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <iomanip>

using namespace doly::extio;

static std::atomic<bool> running{true};

void signal_handler(int) {
    running.store(false);
}

// 将 16-bit 状态格式化为二进制字符串
std::string format_binary(uint16_t value) {
    std::string result;
    for (int i = 15; i >= 0; i--) {
        result += ((value >> i) & 1) ? '1' : '0';
        if (i % 4 == 0 && i != 0) result += '_';
    }
    return result;
}

int main() {
    std::cout << "=== PCA9535 完整功能测试 ===" << std::endl;
    std::cout << "测试内容：" << std::endl;
    std::cout << "  1. YAML 配置加载" << std::endl;
    std::cout << "  2. 原始状态发布 (16-bit)" << std::endl;
    std::cout << "  3. 触摸历史数据发布" << std::endl;
    std::cout << "  4. 悬崖历史数据发布" << std::endl;
    std::cout << "按 Ctrl+C 退出..." << std::endl;
    std::cout << std::endl;

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // 创建服务（会自动加载 YAML 配置）
    Pca9535Service service;

    // 初始化
    if (!service.init()) {
        std::cerr << "初始化失败！" << std::endl;
        return 1;
    }

    // 订阅原始状态
    service.subscribe_raw_state([](const RawStateEvent& event) {
        std::cout << "📊 [RAW] State: 0x" << std::hex << std::setw(4) << std::setfill('0') 
                  << event.state << std::dec 
                  << " (" << format_binary(event.state) << ")" << std::endl;
    });

    // 订阅触摸历史数据
    service.subscribe_touch_history(Pca9535Pin::TOUCH_L, 
        [](const TouchHistoryEvent& event) {
            std::cout << "🖐️  [TOUCH_L HISTORY] " << event.history.size() << " samples:" << std::endl;
            std::cout << "    最近 10 个: ";
            size_t start = event.history.size() > 10 ? event.history.size() - 10 : 0;
            for (size_t i = start; i < event.history.size(); i++) {
                std::cout << (event.history[i] ? "1" : "0");
            }
            std::cout << std::endl;
        });

    service.subscribe_touch_history(Pca9535Pin::TOUCH_R, 
        [](const TouchHistoryEvent& event) {
            std::cout << "🖐️  [TOUCH_R HISTORY] " << event.history.size() << " samples:" << std::endl;
            std::cout << "    最近 10 个: ";
            size_t start = event.history.size() > 10 ? event.history.size() - 10 : 0;
            for (size_t i = start; i < event.history.size(); i++) {
                std::cout << (event.history[i] ? "1" : "0");
            }
            std::cout << std::endl;
        });

    // 订阅悬崖历史数据（4 路）
    auto cliff_history_cb = [](const std::string& name) {
        return [name](const CliffHistoryEvent& event) {
            std::cout << "⚠️  [" << name << " HISTORY] " << event.history.size() << " samples:" << std::endl;
            std::cout << "    最近 10 个: ";
            size_t start = event.history.size() > 10 ? event.history.size() - 10 : 0;
            for (size_t i = start; i < event.history.size(); i++) {
                std::cout << (event.history[i] ? "1" : "0");
            }
            std::cout << std::endl;
        };
    };

    service.subscribe_cliff_history(Pca9535Pin::IRS_FL, cliff_history_cb("IRS_FL"));
    service.subscribe_cliff_history(Pca9535Pin::IRS_FR, cliff_history_cb("IRS_FR"));
    service.subscribe_cliff_history(Pca9535Pin::IRS_BL, cliff_history_cb("IRS_BL"));
    service.subscribe_cliff_history(Pca9535Pin::IRS_BR, cliff_history_cb("IRS_BR"));

    // 订阅触摸手势
    service.subscribe_touch(Pca9535Pin::TOUCH_L, 
        [](const TouchGestureEvent& event) {
            std::cout << "👆 [GESTURE] TOUCH_L - ";
            switch (event.gesture) {
                case TouchGesture::SingleTap:  std::cout << "SingleTap"; break;
                case TouchGesture::DoubleTap:  std::cout << "DoubleTap"; break;
                case TouchGesture::LongPress:  std::cout << "LongPress"; break;
                default: break;
            }
            std::cout << std::endl;
        });

    service.subscribe_touch(Pca9535Pin::TOUCH_R, 
        [](const TouchGestureEvent& event) {
            std::cout << "👆 [GESTURE] TOUCH_R - ";
            switch (event.gesture) {
                case TouchGesture::SingleTap:  std::cout << "SingleTap"; break;
                case TouchGesture::DoubleTap:  std::cout << "DoubleTap"; break;
                case TouchGesture::LongPress:  std::cout << "LongPress"; break;
                default: break;
            }
            std::cout << std::endl;
        });

    // 启动服务
    if (!service.start()) {
        std::cerr << "启动失败！" << std::endl;
        return 1;
    }

    // 主循环：每 5 秒显示统计
    auto last_stats_time = std::chrono::steady_clock::now();

    while (running.load()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));

        auto now = std::chrono::steady_clock::now();
        if (now - last_stats_time >= std::chrono::seconds(5)) {
            auto stats = service.get_stats();
            std::cout << "\n[统计] IRQ: " << stats.irq_count 
                      << ", 引脚事件: " << stats.pin_events
                      << ", 触摸手势: " << stats.touch_gestures
                      << ", 悬崖模式: " << stats.cliff_patterns << "\n" << std::endl;
            last_stats_time = now;
        }
    }

    // 停止服务
    service.stop();
    std::cout << "\n测试结束" << std::endl;

    return 0;
}
