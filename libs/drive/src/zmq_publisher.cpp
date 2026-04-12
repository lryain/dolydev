/**
 * @file zmq_publisher.cpp
 * @brief libs/drive 内部 ZeroMQ 发布器实现
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/
#include "doly/zmq_publisher.hpp"

#include <cerrno>
#include <iostream>
#include <cstring>

namespace doly {

ZmqPublisher::ZmqPublisher(const std::string& endpoint, const std::string& name, bool bind_endpoint) noexcept
    : endpoint_(endpoint), name_(name), bind_endpoint_(bind_endpoint) {
    context_ = zmq_ctx_new();
    if (!context_) {
        std::cerr << "[" << name_ << "] Failed to create ZMQ context" << std::endl;
        return;
    }

    socket_ = zmq_socket(context_, ZMQ_PUB);
    if (!socket_) {
        std::cerr << "[" << name_ << "] Failed to create ZMQ PUB socket" << std::endl;
        close();
        return;
    }

    int linger = 0;
    zmq_setsockopt(socket_, ZMQ_LINGER, &linger, sizeof(linger));

    const int rc = bind_endpoint_ ? zmq_bind(socket_, endpoint_.c_str()) : zmq_connect(socket_, endpoint_.c_str());
    if (rc != 0) {
        const char* verb = bind_endpoint_ ? "bind to" : "connect to";
        std::cerr << "[" << name_ << "] Failed to " << verb << " " << endpoint_ << ": "
                  << zmq_strerror(zmq_errno()) << std::endl;
        close();
        return;
    }

    ready_ = true;
}

ZmqPublisher::~ZmqPublisher() {
    close();
}

bool ZmqPublisher::publish(const std::string& topic, const void* data, size_t size) noexcept {
    if (!ready_ || !socket_) {
        return false;
    }

    if (topic.empty()) {
        return false;
    }

    const int flags_topic = ZMQ_SNDMORE | ZMQ_DONTWAIT;
    if (zmq_send(socket_, topic.data(), topic.size(), flags_topic) < 0) {
        if (zmq_errno() != EAGAIN) {
            std::cerr << "[" << name_ << "] Failed to send topic: "
                      << zmq_strerror(zmq_errno()) << std::endl;
        }
        return false;
    }

    const int flags_payload = ZMQ_DONTWAIT;
    if (zmq_send(socket_, data, size, flags_payload) < 0) {
        if (zmq_errno() != EAGAIN) {
            std::cerr << "[" << name_ << "] Failed to send payload: "
                      << zmq_strerror(zmq_errno()) << std::endl;
        }
        return false;
    }

    return true;
}

void ZmqPublisher::close() noexcept {
    if (socket_) {
        zmq_close(socket_);
        socket_ = nullptr;
    }
    if (context_) {
        zmq_ctx_destroy(context_);
        context_ = nullptr;
    }
    ready_ = false;
}

} // namespace doly