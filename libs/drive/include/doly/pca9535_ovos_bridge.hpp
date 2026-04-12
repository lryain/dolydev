/**
 * @file pca9535_ovos_bridge.hpp
 * @brief PCA9535 ↔ OVOS 控制桥接
 * 
 * Phase 4: 使用独立的 ZeroMQ SUB socket 订阅控制命令
 */
#pragma once

#include "doly/pca9535_service.hpp"
// #include "doly/ovos_bus.hpp"
#include <atomic>
#include <thread>
#include <nlohmann/json.hpp>
#include <zmq.h>  // ZeroMQ C API

namespace doly {
namespace extio {

class Pca9535OvosBridge {
public:
    explicit Pca9535OvosBridge(Pca9535Service& service);
    ~Pca9535OvosBridge();

    bool start();
    void stop();

private:
    // 订阅线程函数
    void subscriber_thread();
    
    // 处理控制命令
    bool handle_control_topic(const std::string& topic, const nlohmann::json& payload);

    Pca9535Service& service_;
    std::atomic<bool> running_{false};
    std::thread thread_;
    
    // ZeroMQ 上下文和 socket（在线程内创建和销毁）
    void* zmq_context_ = nullptr;
    void* zmq_socket_ = nullptr;
};

} // namespace extio
} // namespace doly
