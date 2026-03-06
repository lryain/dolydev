/**
 * @file xiaozhi_zmq_publisher.cc
 * @brief 小智客户端 ZMQ 发布器实现
 */

#include "xiaozhi_zmq_publisher.h"
#include <zmq.hpp>
#include <iostream>
#include <sstream>

namespace xiaozhi {

// ==================== 单例实现 ====================

XiaozhiZmqPublisher& XiaozhiZmqPublisher::instance() {
    static XiaozhiZmqPublisher instance;
    return instance;
}

XiaozhiZmqPublisher::XiaozhiZmqPublisher() {
}

XiaozhiZmqPublisher::~XiaozhiZmqPublisher() {
    stop();
}

// ==================== 初始化/停止 ====================

bool XiaozhiZmqPublisher::init(const std::string& endpoint) {
    if (initialized_) {
        std::cout << "[XiaozhiZmq] Already initialized" << std::endl;
        return true;
    }

    try {
        // 创建 ZMQ 上下文
        context_ = std::make_shared<zmq::context_t>(1);
        
        // 创建发布者套接字
        publisher_ = std::make_shared<zmq::socket_t>(*context_, ZMQ_PUB);
        
        // 连接到消息总线
        publisher_->connect(endpoint);
        
        std::cout << "[XiaozhiZmq] Initialized and connected to " << endpoint << std::endl;
        initialized_ = true;
        return true;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "[XiaozhiZmq] Failed to initialize: " << e.what() << std::endl;
        return false;
    }
}

void XiaozhiZmqPublisher::stop() {
    if (!initialized_) {
        return;
    }

    try {
        if (publisher_) {
            publisher_->close();
            publisher_.reset();
        }
        if (context_) {
            context_->close();
            context_.reset();
        }
        initialized_ = false;
        std::cout << "[XiaozhiZmq] Stopped" << std::endl;
    } catch (const zmq::error_t& e) {
        std::cerr << "[XiaozhiZmq] Error during stop: " << e.what() << std::endl;
    }
}

// ==================== 核心发布功能 ====================

bool XiaozhiZmqPublisher::publish(const std::string& topic, const json& message) {
    if (!initialized_ || !publisher_) {
        std::cerr << "[XiaozhiZmq] Not initialized" << std::endl;
        return false;
    }

    try {
        // 格式：topic + 空格 + JSON
        std::string full_message = topic + " " + message.dump();
        
        zmq::message_t zmq_msg(full_message.size());
        memcpy(zmq_msg.data(), full_message.c_str(), full_message.size());
        
        publisher_->send(zmq_msg, zmq::send_flags::dontwait);
        
        // 日志（可选，生产环境可关闭）
        // std::cout << "[XiaozhiZmq] Published to " << topic << ": " 
        //           << message.dump() << std::endl;
        
        return true;
        
    } catch (const zmq::error_t& e) {
        std::cerr << "[XiaozhiZmq] Failed to publish: " << e.what() << std::endl;
        return false;
    }
}

// ==================== 处理 LLM 响应 ====================

void XiaozhiZmqPublisher::process_llm_response(const json& llm_response) {
    try {
        // 1. 提取并发布情绪（兼容旧格式）
        if (llm_response.contains("emotion")) {
            std::string emotion = llm_response["emotion"].get<std::string>();
            publish_emotion(emotion, "xiaozhi", 5);
        }

        // 2. 处理结构化响应
        if (llm_response.contains("structured")) {
            const json& structured = llm_response["structured"];

            // 2.1 发布情绪（新格式，会覆盖旧格式）
            if (structured.contains("emotion")) {
                std::string emotion = structured["emotion"].get<std::string>();
                int intensity = structured.value("emotion_intensity", 5);
                publish_emotion(emotion, "xiaozhi", intensity);
            }

            // 2.2 发布动作指令
            if (structured.contains("actions") && structured["actions"].is_array()) {
                for (const auto& action : structured["actions"]) {
                    if (action.contains("type") && action.contains("params")) {
                        std::string action_type = action["type"].get<std::string>();
                        json params = action["params"];
                        int priority = action.value("priority", 5);
                        
                        publish_action(action_type, params, priority);
                    }
                }
            }

            // 2.3 发布意图指令
            if (structured.contains("intent")) {
                const json& intent = structured["intent"];
                if (intent.contains("name")) {
                    std::string intent_name = intent["name"].get<std::string>();
                    json entities = intent.value("entities", json::object());
                    std::string text = intent.value("text", "");
                    
                    publish_intent(intent_name, entities, text);
                }
            }
        }

    } catch (const std::exception& e) {
        std::cerr << "[XiaozhiZmq] Error processing LLM response: " 
                  << e.what() << std::endl;
    }
}

// ==================== 专用发布方法 ====================

void XiaozhiZmqPublisher::publish_emotion(const std::string& emotion,
                                         const std::string& source,
                                         int intensity) {
    json msg = {
        {"emotion", emotion},
        {"source", source},
        {"intensity", intensity},
        {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
    };

    publish("emotion.xiaozhi", msg);
    
    std::cout << "[XiaozhiZmq] 发布情绪: " << emotion 
              << " (source=" << source << ", intensity=" << intensity << ")" 
              << std::endl;
}

void XiaozhiZmqPublisher::publish_action(const std::string& action_type,
                                        const json& params,
                                        int priority) {
    json msg = {
        {"action", action_type},
        {"params", params},
        {"priority", priority},
        {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
    };

    publish("cmd.xiaozhi.action", msg);
    
    std::cout << "[XiaozhiZmq] 发布动作: " << action_type 
              << " (priority=" << priority << ")" << std::endl;
}

void XiaozhiZmqPublisher::publish_intent(const std::string& intent_name,
                                        const json& entities,
                                        const std::string& text) {
    json msg = {
        {"intent", intent_name},
        {"entities", entities},
        {"timestamp", std::chrono::system_clock::now().time_since_epoch().count()}
    };

    if (!text.empty()) {
        msg["text"] = text;
    }

    publish("cmd.xiaozhi.intent", msg);
    
    std::cout << "[XiaozhiZmq] 发布意图: " << intent_name << std::endl;
}

} // namespace xiaozhi
