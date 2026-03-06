/**
 * @file test_pca9535_ovos_bridge.cpp
 * @brief Test PCA9535 → OVOS bridge start/stop and control hook
 * 
 * Phase 4: 使用 ZeroMQ 替代 HighSpeedBus
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/pca9535_service.hpp"
#include "doly/pca9535_config_v2.hpp"
#include "doly/pca9535_ovos_bridge.hpp"
#include "doly/zmq_publisher.hpp"
#include <iostream>
#include <cassert>
#include <thread>
#include <chrono>

using namespace doly::extio;

int main() {
    std::cout << "=== Pca9535 Ovos Bridge Test ===" << std::endl;

    // 使用本地 ZmqPublisher 绑定到 IPC 端点，用于向桥发送控制命令
    const std::string endpoint = "ipc:///tmp/doly_zmq.sock";
    auto publisher = std::make_shared<doly::ZmqPublisher>(endpoint, "test_control_pub", true);
    if (!publisher->is_ready()) {
        std::cerr << "Failed to initialize control publisher" << std::endl;
        return 1;
    }

    // 创建服务（仅初始化，不依赖硬件）
    Pca9535Service service;
    Pca9535ConfigV2 cfg;
    service.init(&cfg);
    // 不需要启动 IRQ 线程

    // 创建桥并启动（桥内部会使用独立的 SUB socket 订阅 ipc:///tmp/doly_zmq.sock）
    Pca9535OvosBridge bridge(service);
    assert(bridge.start());

    // 初始化：确保输出引脚为 false
    bool r_clear = service.set_output(Pca9535Pin::SRV_L_EN, false);
    uint16_t debug_out_before = service.get_output_cache();
    assert(r_clear && ((debug_out_before & (1u << static_cast<uint8_t>(Pca9535Pin::SRV_L_EN))) == 0));

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // (will assert after publishing true below)

    // 测试 publish 到 control 主题能被路由到桥（不验证硬件状态）
    std::string topic = "io.pca9535.control.set_output";
    std::string payload = R"({"pin":"SRV_L_EN","value": true})";
    // 通过本地 publisher 发送控制命令
    publisher->publish(topic, payload.data(), payload.size());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 验证控制消息是否被桥接到服务并生效
    uint16_t mask = (1u << static_cast<uint8_t>(Pca9535Pin::SRV_L_EN));
    uint16_t out_cache = service.get_output_cache();
    assert((out_cache & mask) != 0);

    // 现在发送一个 false 的事件来清除输出
    std::string payload_false = R"({"pin":"SRV_L_EN","value": false})";
    publisher->publish(topic, payload_false.data(), payload_false.size());

    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    uint16_t out_cache2 = service.get_output_cache();
    assert((out_cache2 & mask) == 0);

    // 停止
    bridge.stop();
    // publisher will be cleaned up on exit

    std::cout << "✅ Pca9535 Ovos Bridge test completed" << std::endl;
    return 0;
}
