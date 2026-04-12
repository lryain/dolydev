/**
 * @file zmq_publisher.hpp
 * @brief 简化的 ZeroMQ 发布器（适配 libs/drive 内部状态发布）
 * @note 只依赖 libzmq C API，避免对 doly::bus 依赖
 */
#pragma once

#include <string>
#include <cstddef>

#include <zmq.h>

namespace doly {

class ZmqPublisher {
public:
    /**
     * @brief 构造函数
     * @param endpoint ZeroMQ 端点（推荐 ipc:///tmp/doly_zmq.sock）
     * @param name 描述用于调试
     * @param bind_endpoint 是否主动 bind（默认 true，对应驱动服务即 Publisher）
     */
    explicit ZmqPublisher(const std::string& endpoint,
                          const std::string& name = "ZmqPublisher",
                          bool bind_endpoint = true) noexcept;
    ~ZmqPublisher();

    ZmqPublisher(const ZmqPublisher&) = delete;
    ZmqPublisher& operator=(const ZmqPublisher&) = delete;

    /**
     * @brief 检查发布器是否准备完毕
     */
    bool is_ready() const noexcept { return ready_; }

    /**
     * @brief 发布一条多帧消息（topic + payload）
     */
    bool publish(const std::string& topic, const void* data, size_t size) noexcept;

private:
    void close() noexcept;

    void* context_ = nullptr;
    void* socket_ = nullptr;
    std::string endpoint_;
    std::string name_;
    bool ready_ = false;
    bool bind_endpoint_ = true;
};

} // namespace doly