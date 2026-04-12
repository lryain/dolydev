/**
 * @file pca9535_patterns.hpp
 * @brief PCA9535 模式识别（触摸手势 + 悬崖检测）
 */

#pragma once

#include "pca9535_hal.hpp"
#include <cstdint>
#include <array>
#include <vector>
#include <functional>
#include <mutex>

namespace doly {
namespace extio {

/**
 * @brief 采样点
 */
struct SamplePoint {
    uint64_t ts_us;  ///< 微秒时间戳
    bool value;      ///< 电平状态
};

/**
 * @brief 环形缓冲（时间窗口）
 */
template<size_t N>
class RingBuffer {
public:
    void push(const SamplePoint& sample);
    std::vector<SamplePoint> get_samples(uint32_t duration_ms);
    size_t count_edges(uint32_t duration_ms);
    uint32_t get_duty_cycle(uint32_t duration_ms);  ///< 0~100
    
    /**
     * @brief 获取所有历史数据
     * @return 历史数据向量（按时间排序）
     */
    std::vector<SamplePoint> get_all_samples() const;
    
private:
    std::array<SamplePoint, N> buffer_;
    size_t head_ = 0;
    mutable std::mutex mutex_;
    
    uint64_t now_us() const;
};

/**
 * @brief 触摸手势类型
 */
enum class TouchGesture {
    None,
    SingleTap,   ///< 单击：30~300ms
    DoubleTap,   ///< 双击：两次单击间隔 < 250ms
    LongPress    ///< 长按：≥ 600ms
};

/**
 * @brief 触摸手势事件
 */
struct TouchGestureEvent {
    Pca9535Pin pin;           ///< TOUCH_L / TOUCH_R
    TouchGesture gesture;
    uint32_t duration_ms;
    uint64_t ts_us;
};

/**
 * @brief 触摸手势识别器
 */
class TouchGestureRecognizer {
public:
    /**
     * @brief 配置参数
     */
    struct Config {
        uint32_t single_min_ms;      ///< 单击最小时长
        uint32_t single_max_ms;      ///< 单击最大时长
        uint32_t double_interval_ms; ///< 双击间隔
        uint32_t long_press_ms;      ///< 长按阈值
        
        Config() : single_min_ms(30), single_max_ms(300), 
                   double_interval_ms(250), long_press_ms(600) {}
    };

    explicit TouchGestureRecognizer(const Config& config);

    /**
     * @brief 喂入状态变化
     * @param pin 触摸引脚
     * @param value 当前电平
     * @param ts_us 时间戳
     * @return 识别出的手势（None 表示未完成）
     */
    TouchGesture feed(Pca9535Pin pin, bool value, uint64_t ts_us);

    /**
     * @brief 获取最后一次按下的持续时间（读取后自动重置）
     * @param pin 触摸引脚
     * @return 持续时间（毫秒）
     */
    uint32_t get_last_press_duration_ms(Pca9535Pin pin);

    /**
     * @brief 重置状态机
     */
    void reset(Pca9535Pin pin);
    
    /**
     * @brief 更新配置参数
     */
    void set_config(const Config& config) {
        config_ = config;
    }

private:
    enum class State {
        Idle,
        Pressing,
        Released,
        WaitDouble
    };

    struct TouchState {
        State state = State::Idle;
        uint64_t press_start_us = 0;
        uint64_t release_us = 0;
        uint32_t tap_count = 0;
        uint32_t last_press_duration_ms = 0;  ///< 最后一次按下的持续时间
    };

    Config config_;
    std::array<TouchState, 2> states_;  ///< [TOUCH_L, TOUCH_R]

    size_t get_touch_index(Pca9535Pin pin) const;
};

/**
 * @brief 悬崖模式类型
 */
enum class CliffPattern {
    StableFloor,    ///< 稳定地面
    CliffDetected,  ///< 检测到悬崖
    BlackWhiteLine, ///< 黑白线
    Noisy           ///< 噪声
};

/**
 * @brief 悬崖模式事件
 */
struct CliffPatternEvent {
    Pca9535Pin pin;        ///< IRS_* 引脚
    CliffPattern pattern;
    uint64_t ts_us;
};

/**
 * @brief 悬崖模式识别器
 */
class CliffPatternRecognizer {
public:
    /**
     * @brief 配置参数
     */
    struct Config {
        uint32_t window_ms;          ///< 分析窗口
        size_t stable_max_edges;     ///< 稳定状态最大边沿数
        size_t line_min_edges;       ///< 黑白线最小边沿数
        uint8_t cliff_duty_threshold; ///< 悬崖占空比阈值（0-100）
        
        Config() : window_ms(100), stable_max_edges(2), line_min_edges(5), 
                   cliff_duty_threshold(80) {}
    };

    explicit CliffPatternRecognizer(const Config& config);

    /**
     * @brief 喂入状态变化
     * @param pin 悬崖传感器引脚
     * @param value 当前电平
     * @param ts_us 时间戳
     */
    void feed(Pca9535Pin pin, bool value, uint64_t ts_us);

    /**
     * @brief 分析模式
     * @param pin 悬崖传感器引脚
     * @return 识别出的模式
     */
    CliffPattern analyze(Pca9535Pin pin);

    /**
     * @brief 重置
     */
    void reset(Pca9535Pin pin);
    
    /**
     * @brief 更新配置参数
     */
    void set_config(const Config& config) {
        config_ = config;
    }

private:
    Config config_;
    std::array<RingBuffer<128>, 4> histories_;  ///< [IRS_FL, IRS_FR, IRS_BL, IRS_BR]

    size_t get_cliff_index(Pca9535Pin pin) const;
};

} // namespace extio
} // namespace doly
