/*

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

int main() {
    std::cout << "=== PCA9535 批量设置与状态发布测试 ===" << std::endl;

    const std::string endpoint = "ipc:///tmp/doly_zmq_test_io.sock";
    auto publisher = std::make_shared<doly::ZmqPublisher>(endpoint, "test_io_publisher", true);
    
    Pca9535Service service;
    if (!service.init()) return 1;
    if (!service.start()) return 1;

    Pca9535BusAdapter adapter(service, publisher);
    if (!adapter.start()) return 1;

    void* ctx = zmq_ctx_new();
    void* sub = zmq_socket(ctx, ZMQ_SUB);
    zmq_connect(sub, endpoint.c_str());
    zmq_setsockopt(sub, ZMQ_SUBSCRIBE, "io.pca9535.", 11);

    std::thread sub_thread([sub]() {
        while (running.load()) {
            char topic_buf[256];
            char payload_buf[4096];
            int topic_size = zmq_recv(sub, topic_buf, sizeof(topic_buf)-1, ZMQ_DONTWAIT);
            if (topic_size <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
                continue;
            }
            topic_buf[topic_size] = '\0';
            int payload_size = zmq_recv(sub, payload_buf, sizeof(payload_buf)-1, 0);
            if (payload_size > 0) {
                payload_buf[payload_size] = '\0';
                std::cout << "[MSG] Topic=" << topic_buf << " | Payload=" << payload_buf << std::endl;
            }
        }
    });

    std::cout << "\n--- 1. 测试单引脚设置 (SRV_L_EN) ---" << std::endl;
    service.enable_servo_left(true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    service.enable_servo_left(false);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "\n--- 2. 测试批量设置 (SRV_L_EN | SRV_R_EN) ---" << std::endl;
    uint16_t mask = (1u << static_cast<uint8_t>(Pca9535Pin::SRV_L_EN)) | 
                    (1u << static_cast<uint8_t>(Pca9535Pin::SRV_R_EN));
    uint16_t state = mask; // 两者都为 HIGH
    service.set_outputs_bulk(state, mask);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    state = 0; // 两者都为 LOW
    service.set_outputs_bulk(state, mask);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::cout << "\n--- 3. 测试扩展 IO ---" << std::endl;
    service.set_ext_io(0, true);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    service.set_ext_io(0, false);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    std::this_thread::sleep_for(std::chrono::seconds(1));
    running.store(false);
    sub_thread.join();
    zmq_close(sub);
    zmq_ctx_term(ctx);
    adapter.stop();
    service.stop();

    std::cout << "\n测试完成" << std::endl;
    return 0;
}
