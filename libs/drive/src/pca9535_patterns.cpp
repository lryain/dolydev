/**
 * @file pca9535_patterns.cpp
 * @brief 模式识别实现
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/pca9535_patterns.hpp"
#include <chrono>
#include <algorithm>

namespace doly {
namespace extio {

// ==================== RingBuffer ====================

template<size_t N>
uint64_t RingBuffer<N>::now_us() const {
    auto now = std::chrono::high_resolution_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();
}

template<size_t N>
void RingBuffer<N>::push(const SamplePoint& sample) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_[head_] = sample;
    head_ = (head_ + 1) % N;
}

template<size_t N>
std::vector<SamplePoint> RingBuffer<N>::get_samples(uint32_t duration_ms) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    uint64_t now = now_us();
    uint64_t threshold = now - duration_ms * 1000;
    
    std::vector<SamplePoint> result;
    for (size_t i = 0; i < N; i++) {
        size_t idx = (head_ + i) % N;
        if (buffer_[idx].ts_us >= threshold) {
            result.push_back(buffer_[idx]);
        }
    }
    
    return result;
}

template<size_t N>
size_t RingBuffer<N>::count_edges(uint32_t duration_ms) {
    auto samples = get_samples(duration_ms);
    if (samples.size() < 2) {
        return 0;
    }
    
    size_t edges = 0;
    for (size_t i = 1; i < samples.size(); i++) {
        if (samples[i].value != samples[i-1].value) {
            edges++;
        }
    }
    
    return edges;
}

template<size_t N>
uint32_t RingBuffer<N>::get_duty_cycle(uint32_t duration_ms) {
    auto samples = get_samples(duration_ms);
    if (samples.empty()) {
        return 0;
    }
    
    size_t high_count = 0;
    for (const auto& sample : samples) {
        if (sample.value) {
            high_count++;
        }
    }
    
    return (high_count * 100) / samples.size();
}

template<size_t N>
std::vector<SamplePoint> RingBuffer<N>::get_all_samples() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SamplePoint> result;
    result.reserve(N);
    
    // 从 head 开始遍历整个环形缓冲区
    for (size_t i = 0; i < N; i++) {
        size_t idx = (head_ + i) % N;
        if (buffer_[idx].ts_us > 0) {  // 忽略未初始化的条目
            result.push_back(buffer_[idx]);
        }
    }
    
    // 按时间戳排序
    std::sort(result.begin(), result.end(), 
              [](const SamplePoint& a, const SamplePoint& b) {
                  return a.ts_us < b.ts_us;
              });
    
    return result;
}

// 显式实例化
template class RingBuffer<128>;

// ==================== TouchGestureRecognizer ====================

TouchGestureRecognizer::TouchGestureRecognizer(const Config& config)
    : config_(config) {
}

size_t TouchGestureRecognizer::get_touch_index(Pca9535Pin pin) const {
    if (pin == Pca9535Pin::TOUCH_L) return 0;
    if (pin == Pca9535Pin::TOUCH_R) return 1;
    return 0;
}

TouchGesture TouchGestureRecognizer::feed(Pca9535Pin pin, bool value, uint64_t ts_us) {
    size_t idx = get_touch_index(pin);
    auto& state = states_[idx];
    
    // 先检查双击超时（从 WaitDouble 状态返回到 Idle）
    // 这必须在所有状态检查之前进行，以确保超时不被后续事件打断
    if (state.state == State::WaitDouble) {
        uint32_t idle_duration_ms = (ts_us - state.release_us) / 1000;
        if (idle_duration_ms >= config_.double_interval_ms) {
            state.state = State::Idle;
            state.tap_count = 0;
            // SingleTap 的持续时间是第一次按下的时长
            return TouchGesture::SingleTap;
        }
    }
    
    if (value) {  // 按下
        if (state.state == State::Idle) {
            // 新的按下周期开始
            state.state = State::Pressing;
            state.press_start_us = ts_us;
            state.last_press_duration_ms = 0;
        } else if (state.state == State::WaitDouble) {
            // 在等待双击期间收到新的按下 - 这是第二次点击
            uint32_t time_since_release_ms = (ts_us - state.release_us) / 1000;
            
            if (time_since_release_ms < config_.double_interval_ms) {
                // 时间间隔在有效范围内，这是双击的第二次按下
                state.state = State::Pressing;
                state.press_start_us = ts_us;
                state.tap_count = 2;  // 标记为第二次点击
                state.last_press_duration_ms = 0;
            } else {
                // 时间间隔太长，当作新的按下，但先清除等待双击的状态
                state.state = State::Pressing;
                state.press_start_us = ts_us;
                state.tap_count = 0;
                state.last_press_duration_ms = 0;
            }
        }
        return TouchGesture::None;
    } 
    
    // 松开 (value = false)
    if (state.state == State::Pressing) {
        uint32_t press_duration_ms = (ts_us - state.press_start_us) / 1000;
        
        // 检查长按（优先级最高）
        // 注意：长按会取消任何等待中的双击状态
        if (press_duration_ms >= config_.long_press_ms) {
            state.state = State::Idle;
            state.tap_count = 0;  // 重置tap_count，避免遗留双击状态
            state.last_press_duration_ms = press_duration_ms;  // 保存长按时长
            return TouchGesture::LongPress;
        }
        
        // 检查单击/双击（按钮按下时间在有效范围内）
        if (press_duration_ms >= config_.single_min_ms &&
            press_duration_ms <= config_.single_max_ms) {
            
            if (state.tap_count == 0) {
                // 第一次点击
                state.tap_count = 1;
                state.state = State::WaitDouble;
                state.release_us = ts_us;
                state.last_press_duration_ms = press_duration_ms;  // 保存第一次点击的时长
                return TouchGesture::None;  // 等待可能的双击
            } else if (state.tap_count == 2) {
                // 第二次点击确认 - 这是双击
                state.state = State::Idle;
                state.tap_count = 0;
                state.last_press_duration_ms = press_duration_ms;  // 保存第二次点击的时长
                return TouchGesture::DoubleTap;
            }
        } else {
            // 按下时间过短或过长，既不是单击也不是长按
            state.state = State::Idle;
            state.tap_count = 0;
            state.last_press_duration_ms = 0;
            return TouchGesture::None;
        }
    }
    
    return TouchGesture::None;
}

uint32_t TouchGestureRecognizer::get_last_press_duration_ms(Pca9535Pin pin) {
    size_t idx = get_touch_index(pin);
    uint32_t duration = states_[idx].last_press_duration_ms;
    states_[idx].last_press_duration_ms = 0;  // 读取后立即重置
    return duration;
}

void TouchGestureRecognizer::reset(Pca9535Pin pin) {
    size_t idx = get_touch_index(pin);
    states_[idx] = TouchState{};
}

// ==================== CliffPatternRecognizer ====================

CliffPatternRecognizer::CliffPatternRecognizer(const Config& config)
    : config_(config) {
}

size_t CliffPatternRecognizer::get_cliff_index(Pca9535Pin pin) const {
    if (pin == Pca9535Pin::IRS_FL) return 0;
    if (pin == Pca9535Pin::IRS_FR) return 1;
    if (pin == Pca9535Pin::IRS_BL) return 2;
    if (pin == Pca9535Pin::IRS_BR) return 3;
    return 0;
}

void CliffPatternRecognizer::feed(Pca9535Pin pin, bool value, uint64_t ts_us) {
    size_t idx = get_cliff_index(pin);
    SamplePoint sample{ts_us, value};
    histories_[idx].push(sample);
}

CliffPattern CliffPatternRecognizer::analyze(Pca9535Pin pin) {
    size_t idx = get_cliff_index(pin);
    auto& history = histories_[idx];
    
    // 统计边沿数和占空比
    size_t edges = history.count_edges(config_.window_ms);
    uint32_t duty = history.get_duty_cycle(config_.window_ms);
    
    // 黑白线：高频交替
    if (edges >= config_.line_min_edges) {
        return CliffPattern::BlackWhiteLine;
    }
    
    // 稳定状态：低抖动
    if (edges <= config_.stable_max_edges) {
        if (duty < 20) {
            return CliffPattern::CliffDetected;  // 低电平 = 悬崖
        } else {
            return CliffPattern::StableFloor;    // 高电平 = 地面
        }
    }
    
    // 其他情况：噪声
    return CliffPattern::Noisy;
}

void CliffPatternRecognizer::reset(Pca9535Pin /*pin*/) {
    // 环形缓冲自动老化，无需显式清理
}

} // namespace extio
} // namespace doly
