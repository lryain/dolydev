/**
 * @file test_pca9535_sub.cpp
 * @brief PCA9535 消息总线集成测试
 * 
 * Phase 4: 测试 PCA9535Service + ZeroMQ 总线完整链路
 * 使用 SUB 客户端接收来自 PUB 服务器的消息
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include <zmq.hpp>
#include <iostream>
#include <csignal>
#include <thread>
#include <chrono>
#include <cstring>
#include <nlohmann/json.hpp>
#include <atomic>

using json = nlohmann::json;

static std::atomic<bool> running{true};

void signal_handler(int) {
    running.store(false);
}

int main() {
    std::cout << "=== PCA9535 消息总线集成测试 ===" << std::endl;
    std::cout << "按 Ctrl+C 退出..." << std::endl;
    std::cout << std::endl;

    // 注册信号处理
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    std::cout << "\n=== PCA9535 消息总线订阅测试 ===" << std::endl;
    std::cout << "注意: 需要 extio_service 已在另一终端运行并发布事件" << std::endl;
    std::cout << "按 Ctrl+C 退出..." << std::endl;
    std::cout << std::endl;

    // 创建 SUB 客户端来接收来自 PUB 服务器的消息
    // （注意：bus 是 PUB 服务器模式，不能直接订阅）
    std::cout << "\n=== 创建 SUB 客户端 ===" << std::endl;
    
    zmq::context_t ctx;
    zmq::socket_t subscriber(ctx, zmq::socket_type::sub);
    subscriber.connect("ipc:///tmp/doly_zmq.sock");
    
    // 订阅感兴趣的主题（使用 setsockopt 的低级版本）
    const char* prefix = "io.pca9535.";
    subscriber.setsockopt(ZMQ_SUBSCRIBE, prefix, strlen(prefix));
    
    std::cout << "✅ SUB 客户端已连接到 ipc:///tmp/doly_zmq.sock" << std::endl;
    std::cout << "📡 订阅主题前缀: io.pca9535.*" << std::endl;

    std::cout << "\n=== 开始监控 ===" << std::endl;
    std::cout << "请触摸传感器以触发事件..." << std::endl;
    std::cout << std::endl;

    // 主循环：接收 ZMQ 消息
    auto last_stats_time = std::chrono::steady_clock::now();
    int msg_count = 0;

    while (running.load()) {
        // 使用 poll 以支持超时和 Ctrl+C 响应
        zmq::pollitem_t items[] = {{subscriber, 0, ZMQ_POLLIN, 0}};
        int rc = zmq_poll(items, 1, 100);  // 100ms 超时
        
        if (rc > 0 && (items[0].revents & ZMQ_POLLIN)) {
            // 接收消息
            zmq::message_t topic_msg, payload_msg;
            auto res_topic = subscriber.recv(topic_msg, zmq::recv_flags::none);
            auto res_payload = subscriber.recv(payload_msg, zmq::recv_flags::none);
            
            if (res_topic && res_payload) {
                std::string topic(topic_msg.to_string());
                std::string payload(payload_msg.to_string());
                msg_count++;
                
                try {
                    auto j = json::parse(payload);
                    
                    // 按主题类型美化输出
                    if (topic.find("raw.state") != std::string::npos) {
                        std::cout << "📊 [RAW] " << topic << " | state=0x" 
                                  << std::hex << j.value("state", 0) << std::dec << std::endl;
                    }
                    else if (topic.find("touch.gesture") != std::string::npos) {
                        std::cout << "👆 [GESTURE] " << j["gesture"].get<std::string>() 
                                  << " | side=" << j["side"].get<std::string>() 
                                  << " | duration=" << j["duration_ms"].get<int>() << "ms" << std::endl;
                    }
                    else if (topic.find("pin.change") != std::string::npos) {
                        std::cout << "📍 [PIN] " << j["pin"].get<std::string>() 
                                  << " = " << j["value"].get<int>() << std::endl;
                    }
                    else if (topic.find("touch.history") != std::string::npos) {
                        std::cout << "🖐️  [HISTORY] " << topic << " | samples=" 
                                  << j["history"].size() << std::endl;
                    }
                    else if (topic.find("cliff.pattern") != std::string::npos) {
                        std::cout << "⚠️  [CLIFF-PATTERN] pattern=" << j["pattern"].get<std::string>() 
                                  << " | position=" << j["position"].get<std::string>() << std::endl;
                    }
                    else if (topic.find("cliff.history") != std::string::npos) {
                        std::cout << "📈 [CLIFF-HISTORY] " << topic << " | samples=" 
                                  << j["history"].size() << std::endl;
                    }
                    else {
                        std::cout << "📨 [" << topic << "] " << payload.substr(0, 80) 
                                  << (payload.size() > 80 ? "..." : "") << std::endl;
                    }
                    std::cout.flush();
                } catch (const std::exception& e) {
                    std::cout << "⚠️  [" << topic << "] Parse error: " << e.what() << std::endl;
                }
            }
        }

        // 每 5 秒显示统计
        auto now = std::chrono::steady_clock::now();
        if (now - last_stats_time >= std::chrono::seconds(5)) {
            std::cout << "\n[统计] 已接收 " << msg_count << " 条消息" << std::endl;
            last_stats_time = now;
        }
    }

    // 停止服务
    std::cout << "\n正在停止..." << std::endl;
    subscriber.close();
    std::cout << "测试结束" << std::endl;

    return 0;
}
