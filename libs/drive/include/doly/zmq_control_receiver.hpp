/**
 * @file zmq_control_receiver.hpp
 * @brief ZeroMQ 控制命令接收器 (接收来自 doly_webserver 的控制命令)
 * @details 使用 PULL 模式接收来自 WebUI 的控制命令
 * @date 2025-11-17
 * 
 * Phase 4: 双向通信
 */

#pragma once

#include <string>
#include <memory>
#include <functional>
#include <thread>
#include <atomic>
#include <nlohmann/json.hpp>

// Forward declaration
namespace zmq {
    class context_t;
    class socket_t;
}

namespace doly {
namespace extio {

using json = nlohmann::json;

/**
 * @brief ZeroMQ 控制命令接收器
 * 
 * 功能：
 * - 通过 ZeroMQ PULL socket 接收控制命令
 * - 异步处理控制命令
 * - 调用用户提供的回调处理
 */
class ZmqControlReceiver {
public:
    // 回调类型：接收控制命令
    using ControlCallback = std::function<void(const std::string& topic, const json& command)>;
    
    /**
     * @brief 构造函数
     * @param endpoint ZeroMQ PULL 端点 (如 "ipc:///tmp/doly_control.sock")
     * @param callback 控制命令回调函数
     */
    explicit ZmqControlReceiver(const std::string& endpoint = "ipc:///tmp/doly_control.sock",
                               ControlCallback callback = nullptr);
    
    ~ZmqControlReceiver();
    
    // 禁止拷贝
    ZmqControlReceiver(const ZmqControlReceiver&) = delete;
    ZmqControlReceiver& operator=(const ZmqControlReceiver&) = delete;
    
    /**
     * @brief 启动接收器
     * @return 成功返回 true
     */
    bool Start();
    
    /**
     * @brief 停止接收器
     */
    void Stop();
    
    /**
     * @brief 检查是否运行中
     */
    bool IsRunning() const { return _running; }
    
    /**
     * @brief 设置控制命令回调
     */
    void SetControlCallback(ControlCallback callback) {
        _callback = callback;
    }

private:
    /**
     * @brief 接收循环（运行在独立线程中）
     */
    void ReceiverLoop();
    
    std::string _endpoint;
    std::unique_ptr<zmq::context_t> _context;
    std::unique_ptr<zmq::socket_t> _socket;
    bool _running = false;
    std::thread _thread;
    ControlCallback _callback;
    std::atomic<int> _errorCount{0};
};

} // namespace extio
} // namespace doly
