/**
 * @file xiaozhi_zmq_publisher.h
 * @brief 小智客户端 ZMQ 发布器 - 发布情绪、动作和意图到 Doly 系统
 */

#ifndef XIAOZHI_ZMQ_PUBLISHER_H
#define XIAOZHI_ZMQ_PUBLISHER_H

#include <string>
#include <memory>
#include "../common/json.hpp"

// 前置声明
namespace zmq {
    class context_t;
    class socket_t;
}

namespace xiaozhi {

using json = nlohmann::json;

/**
 * @brief 小智 ZMQ 发布器
 * 
 * 负责将云端 AI 返回的结构化响应解析并发布到 ZMQ 消息总线：
 * - emotion.xiaozhi: 情绪变化
 * - cmd.xiaozhi.action: 动作指令
 * - cmd.xiaozhi.intent: 意图指令
 */
class XiaozhiZmqPublisher {
public:
    /**
     * @brief 获取单例实例
     */
    static XiaozhiZmqPublisher& instance();

    /**
     * @brief 初始化发布器
     * @param endpoint ZMQ 端点地址（默认 ipc:///tmp/doly_zmq.sock）
     * @return 成功返回 true
     */
    bool init(const std::string& endpoint = "ipc:///tmp/doly_zmq.sock");

    /**
     * @brief 停止发布器
     */
    void stop();

    /**
     * @brief 处理 LLM 响应中的结构化数据
     * @param llm_response LLM 响应的 JSON 对象
     * 
     * 预期格式：
     * {
     *   "type": "llm",
     *   "emotion": "happy",
     *   "structured": {
     *     "emotion": "happy",
     *     "actions": [...],
     *     "intent": {...}
     *   }
     * }
     */
    void process_llm_response(const json& llm_response);

    /**
     * @brief 发布情绪变化
     * @param emotion 情绪名称
     * @param source 情绪来源（默认 "xiaozhi"）
     * @param intensity 情绪强度 0-10（默认 5）
     */
    void publish_emotion(const std::string& emotion, 
                        const std::string& source = "xiaozhi",
                        int intensity = 5);

    /**
     * @brief 发布动作指令
     * @param action_type 动作类型（如 play_animation）
     * @param params 动作参数
     * @param priority 优先级 1-10（默认 5）
     */
    void publish_action(const std::string& action_type,
                       const json& params,
                       int priority = 5);

    /**
     * @brief 发布意图指令
     * @param intent_name 意图名称（如 greeting）
     * @param entities 实体参数
     * @param text 原始文本（可选）
     */
    void publish_intent(const std::string& intent_name,
                       const json& entities = json::object(),
                       const std::string& text = "");

private:
    XiaozhiZmqPublisher();
    ~XiaozhiZmqPublisher();

    // 禁止拷贝
    XiaozhiZmqPublisher(const XiaozhiZmqPublisher&) = delete;
    XiaozhiZmqPublisher& operator=(const XiaozhiZmqPublisher&) = delete;

    /**
     * @brief 发布消息到指定话题
     * @param topic 话题名称
     * @param message JSON 消息
     * @return 成功返回 true
     */
    bool publish(const std::string& topic, const json& message);

    std::shared_ptr<zmq::context_t> context_;
    std::shared_ptr<zmq::socket_t> publisher_;
    bool initialized_ = false;
};

} // namespace xiaozhi

#endif // XIAOZHI_ZMQ_PUBLISHER_H
