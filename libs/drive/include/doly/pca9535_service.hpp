/**
 * @file pca9535_service.hpp
 * @brief PCA9535 扩展 IO 事件服务
 */

#pragma once

#include "pca9535_hal.hpp"
#include "pca9535_patterns.hpp"
#include "pca9535_config_v2.hpp"
#include <functional>
#include <thread>
#include <atomic>
#include <deque>
#include <map>
#include <vector>

namespace doly {
namespace extio {

/**
 * @brief 事件类型
 */
enum class EventType {
    PinChange,       ///< 原始引脚变化
    TouchGesture,    ///< 触摸手势
    CliffPattern,    ///< 悬崖模式
    RawState,        ///< 原始状态（16-bit）
    TouchHistory,    ///< 触摸历史数据
    CliffHistory     ///< 悬崖历史数据
};

/**
 * @brief 引脚变化事件
 */
struct PinChangeEvent {
    Pca9535Pin pin;
    bool value;
    uint64_t ts_us;
};

/**
 * @brief 原始状态事件（所有 16 个 GPIO 的状态）
 */
struct RawStateEvent {
    uint16_t state;      ///< 16-bit 状态字
    uint64_t ts_us;      ///< 时间戳
};

/**
 * @brief 触摸历史数据事件
 */
struct TouchHistoryEvent {
    Pca9535Pin pin;                  ///< 触摸引脚
    std::vector<bool> history;       ///< 历史状态序列
    std::vector<uint64_t> timestamps; ///< 对应时间戳
    uint64_t ts_us;                  ///< 事件时间戳
};

/**
 * @brief 悬崖历史数据事件
 */
struct CliffHistoryEvent {
    Pca9535Pin pin;                  ///< 悬崖引脚
    std::vector<bool> history;       ///< 历史状态序列
    std::vector<uint64_t> timestamps; ///< 对应时间戳
    uint64_t ts_us;                  ///< 事件时间戳
};

/**
 * @brief 事件回调函数
 */
using PinChangeCallback = std::function<void(const PinChangeEvent&)>;
using TouchGestureCallback = std::function<void(const TouchGestureEvent&)>;
using CliffPatternCallback = std::function<void(const CliffPatternEvent&)>;
using RawStateCallback = std::function<void(const RawStateEvent&)>;
using TouchHistoryCallback = std::function<void(const TouchHistoryEvent&)>;
using CliffHistoryCallback = std::function<void(const CliffHistoryEvent&)>;

/**
 * @brief PCA9535 扩展 IO 服务
 * 
 * 功能：
 * 1. IRQ 监控线程：响应 GPIO1 中断
 * 2. Bulk read + diff：计算状态变化
 * 3. 模式识别：触摸手势、悬崖检测
 * 4. 事件发布：集成 Doly 消息总线
 */
class Pca9535Service {
public:
    Pca9535Service();
    ~Pca9535Service();

    /**
     * @brief 初始化服务（使用 v2 配置）
     * @param config v2 配置（可选，如果为空使用默认值）
     * @return 成功返回 true
     */
    bool init(const Pca9535ConfigV2* config = nullptr);
    
    /**
     * @brief 应用 v2 配置到模式识别器
     */
    void apply_config_to_recognizers(const Pca9535ConfigV2& config) {
        TouchGestureRecognizer::Config touch_cfg;
        touch_cfg.single_min_ms = config.touch.single_min_ms;
        touch_cfg.single_max_ms = config.touch.single_max_ms;
        touch_cfg.double_interval_ms = config.touch.double_interval_ms;
        touch_cfg.long_press_ms = config.touch.long_press_ms;
        touch_recognizer_.set_config(touch_cfg);
        
        CliffPatternRecognizer::Config cliff_cfg;
        cliff_cfg.window_ms = config.cliff.window_ms;
        cliff_cfg.stable_max_edges = config.cliff.stable_max_edges;
        cliff_cfg.line_min_edges = config.cliff.line_min_edges;
        cliff_cfg.cliff_duty_threshold = config.cliff.cliff_duty_threshold;
        cliff_recognizer_.set_config(cliff_cfg);
    }
    
    /**
     * @brief 设置 SharedState 指针（用于直接写入事件）
     * @param state SharedState 指针（nullptr 则禁用直接写入）
     */
    void setSharedState(void* state) {
        shared_state_ = state;
    }

    /**
     * @brief 启动 IRQ 监控线程
     * @return 成功返回 true
     */
    bool start();

    /**
     * @brief 停止服务
     */
    void stop();

    /**
     * @brief 订阅原始引脚变化
     * @param pin 引脚
     * @param callback 回调函数
     * @return 订阅 ID
     */
    uint64_t subscribe_pin(Pca9535Pin pin, PinChangeCallback callback);

    /**
     * @brief 订阅触摸手势
     * @param pin TOUCH_L / TOUCH_R
     * @param callback 回调函数
     * @return 订阅 ID
     */
    uint64_t subscribe_touch(Pca9535Pin pin, TouchGestureCallback callback);

    /**
     * @brief 订阅悬崖模式
     * @param pin IRS_* 引脚
     * @param callback 回调函数
     * @return 订阅 ID
     */
    uint64_t subscribe_cliff(Pca9535Pin pin, CliffPatternCallback callback);

    /**
     * @brief 订阅原始状态变化（所有 16 个 GPIO）
     * @param callback 回调函数
     * @return 订阅 ID
     */
    uint64_t subscribe_raw_state(RawStateCallback callback);

    /**
     * @brief 订阅触摸历史数据
     * @param pin TOUCH_L / TOUCH_R
     * @param callback 回调函数
     * @return 订阅 ID
     */
    uint64_t subscribe_touch_history(Pca9535Pin pin, TouchHistoryCallback callback);

    /**
     * @brief 订阅悬崖历史数据
     * @param pin IRS_* 引脚
     * @param callback 回调函数
     * @return 订阅 ID
     */
    uint64_t subscribe_cliff_history(Pca9535Pin pin, CliffHistoryCallback callback);

    /**
     * @brief 取消订阅
     * @param sub_id 订阅 ID
     */
    void unsubscribe(uint64_t sub_id);

    /**
     * @brief 设置输出引脚
     * @param pin 输出引脚
     * @param value 电平状态
     * @return 成功返回 true
     */
    bool set_output(Pca9535Pin pin, bool value);

    /**
     * @brief 启用/禁用左舵机
     * @param enable true=启用，false=禁用
     * @return 成功返回 true
     */
    bool enable_servo_left(bool enable);

    /**
     * @brief 启用/禁用右舵机
     * @param enable true=启用，false=禁用
     * @return 成功返回 true
     */
    bool enable_servo_right(bool enable);

    /**
     * @brief 同时启用/禁用左右舵机
     * @param enable true=启用，false=禁用
     * @return 成功返回 true
     */
    bool enable_servo_both(bool enable);

    /**
     * @brief 启用/禁用 TOF 传感器
     * @param enable true=启用，false=禁用
     * @return 成功返回 true
     */
    bool enable_tof(bool enable);

    /**
     * @brief 启用/禁用悬崖传感器（同时控制 IRS_DRV + IRS_EN）
     * @param enable true=启用，false=禁用
     * @return 成功返回 true
     */
    bool enable_cliff(bool enable);

    /**
     * @brief 设置扩展 IO
     * @param io_num 扩展 IO 编号（0-5）
     * @param value 电平状态
     * @return 成功返回 true
     */
    bool set_ext_io(uint8_t io_num, bool value);

    /**
     * @brief 批量设置输出引脚状态
     * @param state 状态字（16-bit）
     * @param mask 掩码（1=要修改的引脚，0=保持不变）
     * @return 成功返回 true
     */
    bool set_outputs_bulk(uint16_t state, uint16_t mask);

    /**
     * @brief 获取当前状态
     * @return 16-bit 状态字
     */
    uint16_t get_state() const { return current_state_.load(); }

    /**
     * @brief 获取 HAL 的输出缓存（16-bit），用于测试/调试
     */
    uint16_t get_output_cache() const { return hal_.get_output_cache(); }

    /**
     * @brief 检查是否运行中
     */
    bool is_running() const { return running_.load(); }

private:
    /**
     * @brief IRQ 监控线程
     */
    void irq_loop();

    /**
     * @brief 发布引脚变化事件
     */
    void publish_pin_event(const PinChangeEvent& event);

    /**
     * @brief 发布触摸手势事件
     */
    void publish_touch_event(const TouchGestureEvent& event);

    /**
     * @brief 发布悬崖模式事件
     */
    void publish_cliff_event(const CliffPatternEvent& event);

    /**
     * @brief 发布原始状态事件
     */
    void publish_raw_state_event(const RawStateEvent& event);

    /**
     * @brief 发布触摸历史数据事件
     */
    void publish_touch_history_event(const TouchHistoryEvent& event);

    /**
     * @brief 发布悬崖历史数据事件
     */
    void publish_cliff_history_event(const CliffHistoryEvent& event);

    /**
     * @brief 生成订阅 ID
     */
    uint64_t next_sub_id();

    // HAL 层
    Pca9535Hal hal_;

    // 配置
    Pca9535ConfigV2 config_;

    // 模式识别器
    TouchGestureRecognizer touch_recognizer_;
    CliffPatternRecognizer cliff_recognizer_;
    
    // SharedState 指针（用于直接写入事件）
    void* shared_state_ = nullptr;

    // 历史数据缓冲区
    RingBuffer<128> touch_left_history_;
    RingBuffer<128> touch_right_history_;
    RingBuffer<128> cliff_fl_history_;
    RingBuffer<128> cliff_fr_history_;
    RingBuffer<128> cliff_bl_history_;
    RingBuffer<128> cliff_br_history_;

    // 悬崖确认状态（用于二次确认以过滤毛刺）
    struct CliffConfirmationState {
        std::deque<std::pair<uint64_t, bool>> history; // (ms timestamp, is_cliff)
        uint32_t window_ms = 200;      // 确认窗口（ms），可由 config_ 覆盖
        uint32_t min_samples = 5;      // 最少 CLIFF 样本数
        float cliff_ratio = 0.7f;      // CLIFF 占比阈值

        void set_config(uint32_t w, uint32_t m, float r) {
            window_ms = w; min_samples = m; cliff_ratio = r;
        }

        bool should_publish_cliff(uint64_t ts_us, bool is_cliff) {
            uint64_t now_ms = ts_us / 1000;
            history.emplace_back(now_ms, is_cliff);

            // 移除窗口外的数据
            while (!history.empty() && (now_ms - history.front().first) > window_ms) {
                history.pop_front();
            }

            uint32_t total = (uint32_t)history.size();
            if (total == 0) return false;

            uint32_t cliff_count = 0;
            for (auto &p : history) if (p.second) cliff_count++;

            if (cliff_count >= min_samples) {
                float ratio = (float)cliff_count / (float)total;
                return ratio >= cliff_ratio;
            }
            return false;
        }
    };

    CliffConfirmationState cliff_confirm_state_;

    // 状态
    std::atomic<uint16_t> current_state_{0};
    std::atomic<bool> running_{false};

    // IRQ 线程
    std::thread irq_thread_;

    // 订阅管理
    struct Subscription {
        uint64_t id;
        EventType type;
        Pca9535Pin pin;
        PinChangeCallback pin_cb;
        TouchGestureCallback touch_cb;
        CliffPatternCallback cliff_cb;
        RawStateCallback raw_state_cb;
        TouchHistoryCallback touch_history_cb;
        CliffHistoryCallback cliff_history_cb;
    };

    std::vector<Subscription> subscriptions_;
    std::mutex sub_mutex_;
    std::atomic<uint64_t> next_sub_id_{1};

    // 统计信息
    struct Stats {
        uint64_t irq_count = 0;
        uint64_t pin_events = 0;
        uint64_t touch_gestures = 0;
        uint64_t cliff_patterns = 0;
    };
    Stats stats_;
    mutable std::mutex stats_mutex_;

public:
    /**
     * @brief 获取统计信息
     */
    Stats get_stats() const {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        return stats_;
    }
};

} // namespace extio
} // namespace doly
