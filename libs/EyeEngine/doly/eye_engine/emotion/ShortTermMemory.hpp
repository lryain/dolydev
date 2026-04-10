#pragma once

#include <deque>
#include <string>
#include <cstdint>
#include <vector>

namespace eye_engine::emotion {

/**
 * @struct MemoryEvent
 * @brief 一个记忆事件 (情绪影响事件)
 */
struct MemoryEvent {
    uint64_t timestamp;           // 事件发生时间(ms)
    std::string event_type;       // touch/voice/success/error/timeout/wake_up
    float valence_delta;          // 情绪变化量
    float arousal_delta;
    std::string context;          // 事件上下文描述
};

/**
 * @struct EmotionTrend
 * @brief 情绪趋势分析结果
 */
struct EmotionTrend {
    float valence_trend;          // -1.0~1.0, 上升或下降趋势
    float arousal_trend;
    float consistency;            // 0.0-1.0, 事件的一致性
    int event_count;              // 参考事件数量
};

/**
 * @class ShortTermMemory
 * @brief 短期记忆系统 (FIFO, 最多100条事件)
 * 
 * 用途:
 * 1. 记录最近的交互事件
 * 2. 计算最近的情绪趋势
 * 3. 判断是否持续某种情绪状态
 */
class ShortTermMemory {
public:
    ShortTermMemory();
    
    /**
     * @brief 记录一个事件
     */
    void recordEvent(const std::string& event_type,
                    float valence_delta,
                    float arousal_delta,
                    const std::string& context = "");
    
    /**
     * @brief 清空所有记忆
     */
    void clear();
    
    /**
     * @brief 获取最近N个事件的情绪趋势
     * @param lookback_count 回溯事件数量
     */
    EmotionTrend calculateTrend(int lookback_count = 20) const;
    
    /**
     * @brief 获取所有记忆事件 (用于调试/分析)
     */
    const std::deque<MemoryEvent>& getAllEvents() const {
        return events_;
    }
    
    /**
     * @brief 获取最近的N条事件
     */
    std::vector<MemoryEvent> getRecentEvents(int count = 10) const;
    
    /**
     * @brief 获取当前记忆大小
     */
    size_t size() const { return events_.size(); }
    
private:
    std::deque<MemoryEvent> events_;  // FIFO队列
    static const int MAX_EVENTS = 100;
    
    /**
     * @brief 计算标准差
     */
    float calculateStdDev(const std::vector<float>& values) const;
};

} // namespace eye_engine::emotion
