/**
 * @file zmq_control_receiver.cpp
 * @brief ZeroMQ 控制命令接收器实现
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/zmq_control_receiver.hpp"
#include <zmq.hpp>
#include <iostream>
#include <chrono>
#include <iomanip>

namespace doly {
namespace extio {

namespace {
    void LogInfo(const std::string& msg) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::cout << "[" << std::put_time(std::localtime(&time), "%H:%M:%S") 
                  << "] [ZmqControlReceiver] " << msg << std::endl;
    }
    
    void LogError(const std::string& msg) {
        auto now = std::chrono::system_clock::now();
        auto time = std::chrono::system_clock::to_time_t(now);
        std::cerr << "[" << std::put_time(std::localtime(&time), "%H:%M:%S") 
                  << "] [ZmqControlReceiver] ERROR: " << msg << std::endl;
    }
}

ZmqControlReceiver::ZmqControlReceiver(const std::string& endpoint,
                                       ControlCallback callback)
    : _endpoint(endpoint), _callback(callback) {
    LogInfo("Initialized (not yet started)");
}

ZmqControlReceiver::~ZmqControlReceiver() {
    if (IsRunning()) {
        Stop();
    }
    LogInfo("Destroyed");
}

bool ZmqControlReceiver::Start() {
    if (IsRunning()) {
        LogError("Already running");
        return false;
    }
    
    try {
        // 创建 ZeroMQ context 和 PULL socket
        _context = std::make_unique<zmq::context_t>(1);
        _socket = std::make_unique<zmq::socket_t>(*_context, ZMQ_PULL);
        
        // 绑定到控制命令端点（服务端绑定）
        _socket->bind(_endpoint);
        
        // 设置接收超时
        int rcvtimeo = 1000;  // 1秒
        _socket->setsockopt(ZMQ_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));
        
        _running = true;
        _thread = std::thread(&ZmqControlReceiver::ReceiverLoop, this);
        
        LogInfo("Started, PULL endpoint: " + _endpoint);
        return true;
        
    } catch (const std::exception& e) {
        LogError("Start failed: " + std::string(e.what()));
        _running = false;
        return false;
    }
}

void ZmqControlReceiver::Stop() {
    if (!IsRunning()) {
        return;
    }
    
    try {
        _running = false;
        
        if (_thread.joinable()) {
            _thread.join();
        }
        
        _socket.reset();
        _context.reset();
        
        LogInfo("Stopped");
        
    } catch (const std::exception& e) {
        LogError("Stop error: " + std::string(e.what()));
    }
}

void ZmqControlReceiver::ReceiverLoop() {
    LogInfo("Receiver loop started");
    
    try {
        int loopCount = 0;
        while (_running) {
            try {
                // 每 100 次循环打印一次心跳
                if (++loopCount % 100 == 0) {
                    LogInfo("Receiver loop heartbeat (count=" + std::to_string(loopCount) + ")");
                }
                
                // 接收 topic (第一部分)
                zmq::message_t topicMsg;
                auto topicSize = _socket->recv(topicMsg, zmq::recv_flags::none);

                if (!topicSize) {
                    // timeout or maybe shutdown
                    continue;
                }

                std::string topic(static_cast<char*>(topicMsg.data()), topicMsg.size());

                // There are two historical formats in the system:
                // 1) Two-frame: topic (frame 1) + JSON payload (frame 2)
                // 2) Single-frame: "topic JSON" in one frame (legacy scripts)
                // Accept both. If the topic frame contains a space and looks like it
                // contains JSON, split it.
                std::string data;
                // Check whether the current message has more frames
                bool hasMore = false;
                try {
                    int more = 0;
                    size_t more_size = sizeof(more);
                    _socket->getsockopt(ZMQ_RCVMORE, &more, &more_size);
                    hasMore = (more != 0);
                } catch (const zmq::error_t& e) {
                    // If the option read fails, assume there are no more frames.
                    hasMore = false;
                }

                size_t splitPos = topic.find(' ');
                if (splitPos != std::string::npos) {
                    // Legacy single-frame format: topic + <space> + JSON payload
                    std::string maybeTopic = topic.substr(0, splitPos);
                    std::string maybeData = topic.substr(splitPos + 1);
                    if ((maybeTopic.rfind("io.", 0) == 0 || maybeTopic.rfind("cmd.", 0) == 0)
                        && !maybeData.empty() && maybeData.front() == '{') {
                        LogInfo("Received single-frame topic+payload");
                        topic = maybeTopic;
                        data = maybeData;
                        // In this case, topic was the whole message, so there are no further frames
                        hasMore = false;
                    }
                }

                if (hasMore && data.empty()) {
                    // Normal multi-frame case: receive payload as second frame
                    zmq::message_t dataMsg;
                    auto dataSize = _socket->recv(dataMsg, zmq::recv_flags::none);
                    if (!dataSize) {
                        LogInfo("Received empty data message after topic, continuing...");
                        continue;
                    }
                    data.assign(static_cast<char*>(dataMsg.data()), dataMsg.size());
                } else if (data.empty() && !hasMore) {
                    // No more frames and no inline payload – this is a topic-only message, which is invalid
                    // LogInfo("Received topic-only or malformed message without payload, skipping: " + topic);
                    continue;
                }
                
                // 跳过空消息
                if (data.empty()) {
                    continue;
                }
                
                // 解析 JSON 并调用回调
                try {
                    auto command = json::parse(data);
                    LogInfo("Received command: " + topic + " | " + data);
                    
                    if (_callback) {
                        _callback(topic, command);
                    }
                } catch (const json::parse_error& e) {
                    LogError("Failed to parse command JSON: [data_size=" + std::to_string(data.size()) + "] " + std::string(e.what()));
                }
                
            } catch (const zmq::error_t& e) {
                if (e.num() != EAGAIN && e.num() != ETERM) {
                    LogError("ZeroMQ error: " + std::string(e.what()));
                    _errorCount++;
                }
            }
        }
        
    } catch (const std::exception& e) {
        LogError("Receiver loop exception: " + std::string(e.what()));
    }
    
    LogInfo("Receiver loop stopped");
}

} // namespace extio
} // namespace doly
