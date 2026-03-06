/**
 * @file test_pca9535_bus.cpp
 * @brief PCA9535 消息总线集成测试
 * 
 * Phase 4: 测试 PCA9535Service + ZeroMQ 总线完整链路
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/pca9535_service.hpp"
#include "doly/pca9535_bus_adapter.hpp"
#include "doly/zmq_publisher.hpp"
#include <zmq.h>
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <nlohmann/json.hpp>

using namespace doly::extio;
using json = nlohmann::json;

static std::atomic<bool> running{true};

void signal_handler(int) {
    running.store(false);
}

int main() {
    std::cout << "=== PCA9535 消息总线集成测试 ===" << std::endl;
    std::cout << "测试链路：PCA9535Service → BusAdapter → ZeroMQ" << std::endl;
    std::cout << "按 Ctrl+C 退出..." << std::endl;
    std::cout << std::endl;

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Phase 4: 使用本地 ZmqPublisher 绑定到 IPC 端点，并把它传入适配器
    const std::string endpoint = "ipc:///tmp/doly_zmq.sock";
    auto publisher = std::make_shared<doly::ZmqPublisher>(endpoint, "test_publisher", true);
    if (!publisher->is_ready()) {
        std::cerr << "Failed to initialize test ZmqPublisher" << std::endl;
        return 1;
    }

    // 创建 PCA9535 服务
    Pca9535Service service;

    // 初始化服务（会自动加载 YAML 配置）
    if (!service.init()) {
        std::cerr << "PCA9535 服务初始化失败！" << std::endl;
        return 1;
    }

    // 启动服务
    if (!service.start()) {
        std::cerr << "PCA9535 服务启动失败！" << std::endl;
        return 1;
    }

    // 创建总线适配器（传入已绑定的 publisher）
    Pca9535BusAdapter adapter(service, publisher);

    // 启动适配器
    if (!adapter.start()) {
        std::cerr << "总线适配器启动失败！" << std::endl;
        service.stop();
        return 1;
    }

    // 使用原生 ZeroMQ SUB socket 订阅并打印消息（在后台线程）
    void* ctx = zmq_ctx_new();
    void* sub = zmq_socket(ctx, ZMQ_SUB);
    zmq_connect(sub, endpoint.c_str());

    // 订阅前缀（所有 io.pca9535.* 事件）
    const std::string prefix = "io.pca9535.";
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, prefix.c_str(), prefix.size());

    std::thread sub_thread([sub]() {
        while (running.load()) {
            char topic_buf[256];
            char payload_buf[4096];
            int topic_size = zmq_recv(sub, topic_buf, sizeof(topic_buf)-1, 0);
            if (topic_size <= 0) continue;
            topic_buf[topic_size] = '\0';
            int payload_size = zmq_recv(sub, payload_buf, sizeof(payload_buf)-1, 0);
            if (payload_size > 0) {
                payload_buf[payload_size] = '\0';
                try {
                    auto j = json::parse(std::string(payload_buf, payload_size));
                    std::cout << "[SUB] Topic=" << topic_buf << " | " << j.dump() << std::endl;
                } catch (...) {
                    std::cout << "[SUB] Topic=" << topic_buf << " | (non-JSON payload)" << std::endl;
                }
            }
        }
    });

    std::cout << "\n=== 开始监控 ===" << std::endl;
    std::cout << "请触摸传感器以触发事件..." << std::endl;
    std::cout << std::endl;

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
    std::cout << "\n正在停止..." << std::endl;
    adapter.stop();
    service.stop();
    // 清理订阅线程和 socket
    zmq_close(sub);
    zmq_ctx_term(ctx);
    sub_thread.join();

    // publisher will be freed on exit
    std::cout << "测试结束" << std::endl;

    return 0;
}
