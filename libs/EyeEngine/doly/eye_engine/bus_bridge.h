#pragma once

#include "doly/eye_engine/engine_control.h"
#include <zmq.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace doly::eye_engine {

/**
 * @brief ZeroMQ 消息总线桥接器
 * 
 * 职责:
 * - 订阅 cmd.eye.* 主题并转换为 EngineCommand
 * - 发布 status.eye.* / event.eye.* 到总线
 * - 与 SharedState 双写（可选）
 */
class BusBridge {
public:
    struct Config {
        std::string sub_endpoint{"ipc:///tmp/doly_bus.sock"};
        std::string pub_endpoint{"ipc:///tmp/doly_bus_pub.sock"};
        bool enable_state_publish{true};
        int state_publish_interval_ms{200};  // 5Hz
        bool debug_raw{false};
        std::string source_id{"eye_engine"};
    };

    explicit BusBridge(const Config& config);
    ~BusBridge();

    // 启动订阅/发布线程
    bool start(CommandQueue* cmd_queue, StatePublisher* state_pub);
    
    // 停止并等待线程退出
    void stop();
    
    // 发布眼睛状态（由 EyeEngine 主循环调用）
    void publishState(const nlohmann::json& state_data);
    
    // 发布事件
    void publishEvent(const std::string& event_type, const nlohmann::json& event_data);
    
    // 统计信息
    struct Stats {
        std::uint64_t commands_received{0};
        std::uint64_t commands_invalid{0};
        std::uint64_t states_published{0};
        std::uint64_t events_published{0};
    };
    Stats getStats() const;

private:
    void subscriberThread();
    void publisherThread();
    
    bool parseCommand(const std::string& topic, const std::string& payload_json,
                     EngineCommand* out_cmd);
    
    nlohmann::json wrapEnvelope(const nlohmann::json& data) const;

    Config config_;
    std::atomic<bool> running_{false};
    
    std::unique_ptr<std::thread> sub_thread_;
    std::unique_ptr<std::thread> pub_thread_;

    std::unique_ptr<zmq::socket_t> event_pub_;
    std::mutex event_pub_mutex_;
    
    CommandQueue* cmd_queue_{nullptr};
    StatePublisher* state_pub_{nullptr};
    
    std::unique_ptr<zmq::context_t> zmq_ctx_;
    
    mutable std::mutex stats_mutex_;
    Stats stats_;
};

}  // namespace doly::eye_engine
