#pragma once

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>

namespace doly::eye_engine {

enum class EngineCommandType {
    kApplyProfile,
    kPlayAnimation,
    kClearDisplay,
    kRestartRenderer,
    kSetBacklight,
    kSetMode,
    kLoadSequence,
    kCustom
};

struct EngineCommand {
    EngineCommandType type{EngineCommandType::kCustom};
    std::string command_name;  // 用于 kCustom 类型的详细命令名
    std::string payload_json;
    std::string source;
    int priority{99};
    std::optional<int64_t> sequence_id;
    std::chrono::steady_clock::time_point enqueue_time{std::chrono::steady_clock::now()};
};

// 🎮 P5-A1: 增加优先级比较器
struct EngineCommandComparator {
    bool operator()(const EngineCommand& a, const EngineCommand& b) const {
        if (a.priority != b.priority) {
            // 数值越小优先级越高，在 priority_queue 中，
            // 返回 true 表示 a 优先级低于 b (b 应该排在 a 前面)，
            // 所以 a.priority > b.priority 时返回 true。
            return a.priority > b.priority;
        }
        // 优先级相同时，早进入队列的排在前面（先进先出）。
        // 返回 true 表示 a 较晚进入队列，优先级低于 b。
        return a.enqueue_time > b.enqueue_time;
    }
};

class CommandQueue {
public:
    void enqueue(EngineCommand command);
    void requeueFront(std::vector<EngineCommand> commands);
    bool tryDequeue(EngineCommand* out_command);
    std::optional<EngineCommand> waitFor(std::chrono::milliseconds timeout);
    std::size_t size() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    // 🔧 P5-A1: 改为优先队列
    std::priority_queue<EngineCommand, std::vector<EngineCommand>, EngineCommandComparator> queue_;
};

struct StateEvent {
    std::string topic;
    std::string payload_json;
    std::chrono::system_clock::time_point timestamp{std::chrono::system_clock::now()};
};

class StatePublisher {
public:
    using SubscriberId = std::uint64_t;
    using Subscriber = std::function<void(const StateEvent&)>;

    SubscriberId subscribe(Subscriber subscriber);
    void unsubscribe(SubscriberId id);
    void publish(const StateEvent& event);

private:
    std::mutex mutex_;
    std::unordered_map<SubscriberId, Subscriber> subscribers_;
    std::atomic<SubscriberId> next_id_{1};
};

}  // namespace doly::eye_engine
