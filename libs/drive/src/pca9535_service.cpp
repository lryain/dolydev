/**
 * @file pca9535_service.cpp
 * @brief PCA9535 服务实现
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/pca9535_service.hpp"
#include "drive/shared_state.hpp"  // ★ 新增：SharedState
#include <iostream>
#include <chrono>
#include <algorithm>

namespace doly {
namespace extio {

Pca9535Service::Pca9535Service() 
    : touch_recognizer_(TouchGestureRecognizer::Config())
    , cliff_recognizer_(CliffPatternRecognizer::Config()) {
}

Pca9535Service::~Pca9535Service() {
    stop();
}

bool Pca9535Service::init(const Pca9535ConfigV2* config) {
    // 获取配置（优先使用传入的，否则使用默认值）
    if (config) {
        config_ = *config;
        std::cout << "[PCA9535 Service] Using provided v2 config" << std::endl;
        
        // 打印加载的配置参数（用于调试）
        std::cout << "[PCA9535 Service] Touch config: single[" 
                  << config_.touch.single_min_ms << "-" 
                  << config_.touch.single_max_ms << "]ms, "
                  << "double_interval=" << config_.touch.double_interval_ms << "ms, "
                  << "long_press=" << config_.touch.long_press_ms << "ms" << std::endl;
        
        std::cout << "[PCA9535 Service] Cliff config: window=" << config_.cliff.window_ms << "ms, "
                  << "stable_max_edges=" << config_.cliff.stable_max_edges << ", "
                  << "line_min_edges=" << config_.cliff.line_min_edges << ", "
                  << "cliff_duty_threshold=" << (int)config_.cliff.cliff_duty_threshold << "%" << std::endl;
    } else {
        // 使用默认配置
        config_ = Pca9535ConfigV2();
        std::cout << "[PCA9535 Service] Using default config" << std::endl;
    }

    if (!hal_.init()) {
        std::cerr << "[PCA9535 Service] HAL init failed" << std::endl;
        return false;
    }

    // 应用默认使能状态
    // P1.3 修改: 不再自动使能舵机电源，防止启动抖动。
    // 由 ServoController 在 Init 时手动触发带 PWM 的上电流程。
    if (config_.enable_tof_default) {
        printf("[PCA9535 Service] Enabling right tof\n");
        enable_tof(true);
    }
    if (config_.enable_cliff_default) {
        printf("[PCA9535 Service] Enabling cliff sensor\n");
        enable_cliff(true);
    }

    std::cout << "[PCA9535 Service] Initialized (servo_left=" 
              << config_.enable_servo_left_default
              << ", servo_right=" << config_.enable_servo_right_default
              << ", tof=" << config_.enable_tof_default
              << ", cliff=" << config_.enable_cliff_default << ")" << std::endl;
    return true;
}

bool Pca9535Service::start() {
    if (running_.load()) {
        std::cerr << "[PCA9535 Service] Already running" << std::endl;
        return false;
    }

    // 读取初始状态
    uint16_t initial_state = hal_.bulk_read();
    current_state_.store(initial_state);
    std::cout << "[PCA9535 Service] Initial state: 0x" 
              << std::hex << initial_state << std::dec << std::endl;

    // 获取时间戳
    auto now = std::chrono::high_resolution_clock::now();
    uint64_t ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
        now.time_since_epoch()).count();

    // 发布初始原始状态事件
    publish_raw_state_event({initial_state, ts_us});

    // 启动 IRQ 线程
    running_.store(true);
    irq_thread_ = std::thread(&Pca9535Service::irq_loop, this);

    std::cout << "[PCA9535 Service] Started" << std::endl;
    return true;
}

void Pca9535Service::stop() {
    if (!running_.load()) {
        return;
    }

    running_.store(false);

    if (irq_thread_.joinable()) {
        irq_thread_.join();
    }
    
    hal_.cleanup();
    std::cout << "[PCA9535 Service] Stopped" << std::endl;
}

void Pca9535Service::irq_loop() {
    uint16_t prev_state = current_state_.load();

    while (running_.load()) {
        // 等待中断（100ms 超时以检查 running_）
        struct timespec timeout = {0, 100000000};  // 100ms
        bool got_irq = hal_.wait_interrupt(&timeout);

        if (!got_irq) {
            continue;  // 超时，继续循环
        }

        // 更新统计
        {
            std::lock_guard<std::mutex> lock(stats_mutex_);
            stats_.irq_count++;
        }

        // Bulk read 当前状态
        uint16_t curr_state = hal_.bulk_read();
        current_state_.store(curr_state);

        // 获取时间戳（事件消息使用高精度时间戳，SharedState 使用统一的 getCurrentTimeMs）
        auto now = std::chrono::high_resolution_clock::now();
        uint64_t ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();

        // 发布原始状态事件（每次 IRQ 都发布）
        RawStateEvent raw_event{curr_state, ts_us};
        publish_raw_state_event(raw_event);

        // 计算差异
        uint16_t diff = curr_state ^ prev_state;

        // 处理每个变化的引脚
        for (uint8_t bit = 0; bit < 16; bit++) {
            if (diff & (1u << bit)) {
                bool value = curr_state & (1u << bit);
                Pca9535Pin pin = static_cast<Pca9535Pin>(bit);

                // 发布原始引脚事件
                PinChangeEvent event{pin, value, ts_us};
                publish_pin_event(event);

                // 触摸手势识别（TOUCH_L / TOUCH_R）
                if (pin == Pca9535Pin::TOUCH_L || pin == Pca9535Pin::TOUCH_R) {
                    // 存储到历史缓冲区
                    SamplePoint sample{ts_us, value};
                    if (pin == Pca9535Pin::TOUCH_L) {
                        touch_left_history_.push(sample);
                    } else {
                        touch_right_history_.push(sample);
                    }

                    // ★ 直接更新 SharedState（原始数据）
                    if (shared_state_) {
                        auto* state = reinterpret_cast<doly::drive::SharedState*>(shared_state_);
                        // 使用 SharedState::getCurrentTimeMs() 以确保时钟基准一致（steady_clock）
                        uint64_t now_ms = doly::drive::SharedState::getCurrentTimeMs();
                        
                        // TTP223 触摸传感器是低电平有效（0=按下，1=松开）
                        if (!value) {  // 按下
                            state->touch.touched = true;
                            state->touch.zone = 1;  // head
                            state->touch.start_time_ms = now_ms;
                        } else {  // 松开
                            state->touch.touched = false;
                            state->touch.duration_ms = now_ms - state->touch.start_time_ms.load();
                        }
                    }

                    // 手势识别
                    // TTP223 触摸传感器在 Doly 上是低电平有效 (Active Low)
                    // 0 = 按下, 1 = 松开
                    // 因此传给识别器时需要取反
                    TouchGesture gesture = touch_recognizer_.feed(pin, !value, ts_us);
                    if (gesture != TouchGesture::None) {
                        uint32_t press_duration_ms = touch_recognizer_.get_last_press_duration_ms(pin);
                        TouchGestureEvent touch_event{pin, gesture, press_duration_ms, ts_us};
                        
                        // ★ 直接更新 SharedState（手势事件）
                        if (shared_state_) {
                            auto* state = reinterpret_cast<doly::drive::SharedState*>(shared_state_);
                            state->touch.gesture_type = static_cast<uint8_t>(gesture);
                            // 使用统一的 SharedState 时间基准
                            state->touch.gesture_time_ms = doly::drive::SharedState::getCurrentTimeMs();
                        }
                        
                        publish_touch_event(touch_event);
                    }

                    // 发布历史数据
                    std::vector<SamplePoint> history = (pin == Pca9535Pin::TOUCH_L) ? 
                        touch_left_history_.get_all_samples() : 
                        touch_right_history_.get_all_samples();
                    
                    TouchHistoryEvent history_event;
                    history_event.pin = pin;
                    history_event.ts_us = ts_us;
                    for (const auto& sample : history) {
                        history_event.history.push_back(sample.value);
                        history_event.timestamps.push_back(sample.ts_us);
                    }
                    publish_touch_history_event(history_event);
                }

                // 悬崖模式识别（IRS_*）
                if (pin == Pca9535Pin::IRS_FL || pin == Pca9535Pin::IRS_FR ||
                    pin == Pca9535Pin::IRS_BL || pin == Pca9535Pin::IRS_BR) {
                    // printf("IRS pin changed: %d -> %d at %llu us\n", pin, value, ts_us);
                    // 存储到历史缓冲区
                    SamplePoint sample{ts_us, value};
                    if (pin == Pca9535Pin::IRS_FL) {
                        cliff_fl_history_.push(sample);
                    } else if (pin == Pca9535Pin::IRS_FR) {
                        cliff_fr_history_.push(sample);
                    } else if (pin == Pca9535Pin::IRS_BL) {
                        cliff_bl_history_.push(sample);
                    } else if (pin == Pca9535Pin::IRS_BR) {
                        cliff_br_history_.push(sample);
                    }

                    // 模式识别
                    cliff_recognizer_.feed(pin, value, ts_us);
                    CliffPattern pattern = cliff_recognizer_.analyze(pin);
                    CliffPatternEvent cliff_event{pin, pattern, ts_us};
                    
                    // ★ 直接更新 SharedState
                    if (shared_state_) {
                        // printf("Updating SharedState for cliff event\n");
                        auto* state = reinterpret_cast<doly::drive::SharedState*>(shared_state_);
                        // 使用统一的 SharedState 时间基准
                        uint64_t now_ms = doly::drive::SharedState::getCurrentTimeMs();
                        
                        state->cliff.pattern_type = static_cast<uint8_t>(pattern);
                        state->cliff.trigger_time_ms = now_ms;
                        
                        // 记录是哪个传感器触发
                        if (pin == Pca9535Pin::IRS_FL || pin == Pca9535Pin::IRS_BL) {
                            state->cliff.sensor = 1;  // left
                            state->cliff.left_triggered = (pattern == CliffPattern::CliffDetected);
                        } else {
                            state->cliff.sensor = 2;  // right
                            state->cliff.right_triggered = (pattern == CliffPattern::CliffDetected);
                        }
                    }
                    
                    // 二次确认：在发布 CLIFF 事件前使用滑动窗口确认以过滤毛刺
                    // 配置优先使用 config_.cliff 中的 window_ms 等，如果存在更精细的确认配置可扩展
                    // 将确认状态的配置对齐到当前 config_
                    cliff_confirm_state_.set_config(
                        config_.cliff.window_ms > 0 ? config_.cliff.window_ms : 200,
                        /*min_samples*/ 5,
                        /*cliff_ratio*/ 0.7f
                    );

                    bool is_cliff = (pattern == CliffPattern::CliffDetected);
                    bool confirmed = false;
                    if (is_cliff) {
                        confirmed = cliff_confirm_state_.should_publish_cliff(ts_us, true);
                    } else {
                        // 仍需推送非悬崖样本以维护历史窗口
                        cliff_confirm_state_.should_publish_cliff(ts_us, false);
                    }

                    if (confirmed) {
                        publish_cliff_event(cliff_event);
                    } else {
                        // 未确认时，仅在调试模式下记录日志（避免噪声过多）
                        // printf("[PCA9535 Service] CLIFF not confirmed (pin=%d)\n", pin);
                    }

                    // 发布历史数据
                    std::vector<SamplePoint> history;
                    if (pin == Pca9535Pin::IRS_FL) {
                        history = cliff_fl_history_.get_all_samples();
                    } else if (pin == Pca9535Pin::IRS_FR) {
                        history = cliff_fr_history_.get_all_samples();
                    } else if (pin == Pca9535Pin::IRS_BL) {
                        history = cliff_bl_history_.get_all_samples();
                    } else if (pin == Pca9535Pin::IRS_BR) {
                        history = cliff_br_history_.get_all_samples();
                    }
                    
                    CliffHistoryEvent history_event;
                    history_event.pin = pin;
                    history_event.ts_us = ts_us;
                    for (const auto& sample : history) {
                        history_event.history.push_back(sample.value);
                        history_event.timestamps.push_back(sample.ts_us);
                    }
                    publish_cliff_history_event(history_event);
                }
            }
        }

        prev_state = curr_state;
    }

    std::cout << "[PCA9535 Service] IRQ loop exited" << std::endl;
}

void Pca9535Service::publish_pin_event(const PinChangeEvent& event) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.pin_events++;
    }

    for (auto& sub : subscriptions_) {
        if (sub.type == EventType::PinChange && sub.pin == event.pin) {
            if (sub.pin_cb) {
                sub.pin_cb(event);
            }
        }
    }
}

void Pca9535Service::publish_touch_event(const TouchGestureEvent& event) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.touch_gestures++;
    }

    for (auto& sub : subscriptions_) {
        if (sub.type == EventType::TouchGesture && sub.pin == event.pin) {
            if (sub.touch_cb) {
                sub.touch_cb(event);
            }
        }
    }
}

void Pca9535Service::publish_cliff_event(const CliffPatternEvent& event) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    {
        std::lock_guard<std::mutex> stats_lock(stats_mutex_);
        stats_.cliff_patterns++;
    }

    for (auto& sub : subscriptions_) {
        if (sub.type == EventType::CliffPattern && sub.pin == event.pin) {
            if (sub.cliff_cb) {
                sub.cliff_cb(event);
            }
        }
    }
}

void Pca9535Service::publish_raw_state_event(const RawStateEvent& event) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    for (auto& sub : subscriptions_) {
        if (sub.type == EventType::RawState) {
            if (sub.raw_state_cb) {
                sub.raw_state_cb(event);
            }
        }
    }
}

void Pca9535Service::publish_touch_history_event(const TouchHistoryEvent& event) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    for (auto& sub : subscriptions_) {
        if (sub.type == EventType::TouchHistory && sub.pin == event.pin) {
            if (sub.touch_history_cb) {
                sub.touch_history_cb(event);
            }
        }
    }
}

void Pca9535Service::publish_cliff_history_event(const CliffHistoryEvent& event) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    for (auto& sub : subscriptions_) {
        if (sub.type == EventType::CliffHistory && sub.pin == event.pin) {
            if (sub.cliff_history_cb) {
                sub.cliff_history_cb(event);
            }
        }
    }
}

uint64_t Pca9535Service::subscribe_pin(Pca9535Pin pin, PinChangeCallback callback) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    uint64_t id = next_sub_id();
    Subscription sub;
    sub.id = id;
    sub.type = EventType::PinChange;
    sub.pin = pin;
    sub.pin_cb = callback;

    subscriptions_.push_back(sub);
    return id;
}

uint64_t Pca9535Service::subscribe_touch(Pca9535Pin pin, TouchGestureCallback callback) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    uint64_t id = next_sub_id();
    Subscription sub;
    sub.id = id;
    sub.type = EventType::TouchGesture;
    sub.pin = pin;
    sub.touch_cb = callback;

    subscriptions_.push_back(sub);
    return id;
}

uint64_t Pca9535Service::subscribe_cliff(Pca9535Pin pin, CliffPatternCallback callback) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    uint64_t id = next_sub_id();
    Subscription sub;
    sub.id = id;
    sub.type = EventType::CliffPattern;
    sub.pin = pin;
    sub.cliff_cb = callback;

    subscriptions_.push_back(sub);
    return id;
}

uint64_t Pca9535Service::subscribe_raw_state(RawStateCallback callback) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    uint64_t id = next_sub_id();
    Subscription sub;
    sub.id = id;
    sub.type = EventType::RawState;
    sub.pin = Pca9535Pin::IRS_FL;  // 占位符
    sub.raw_state_cb = callback;

    subscriptions_.push_back(sub);
    return id;
}

uint64_t Pca9535Service::subscribe_touch_history(Pca9535Pin pin, TouchHistoryCallback callback) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    uint64_t id = next_sub_id();
    Subscription sub;
    sub.id = id;
    sub.type = EventType::TouchHistory;
    sub.pin = pin;
    sub.touch_history_cb = callback;

    subscriptions_.push_back(sub);
    return id;
}

uint64_t Pca9535Service::subscribe_cliff_history(Pca9535Pin pin, CliffHistoryCallback callback) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    uint64_t id = next_sub_id();
    Subscription sub;
    sub.id = id;
    sub.type = EventType::CliffHistory;
    sub.pin = pin;
    sub.cliff_history_cb = callback;

    subscriptions_.push_back(sub);
    return id;
}

void Pca9535Service::unsubscribe(uint64_t sub_id) {
    std::lock_guard<std::mutex> lock(sub_mutex_);

    subscriptions_.erase(
        std::remove_if(subscriptions_.begin(), subscriptions_.end(),
            [sub_id](const Subscription& sub) { return sub.id == sub_id; }),
        subscriptions_.end()
    );
}

bool Pca9535Service::set_output(Pca9535Pin pin, bool value) {
    bool ok = hal_.write_pin(pin, value);
    if (ok) {
        // 更新当前状态字
        uint16_t bit = 1u << static_cast<uint8_t>(pin);
        uint16_t old_state = current_state_.load();
        uint16_t new_state = value ? (old_state | bit) : (old_state & ~bit);
        current_state_.store(new_state);

        std::cout << "[PCA9535 Service] Output pin " << static_cast<int>(pin) 
                  << " set to " << (value ? "HIGH" : "LOW") 
                  << ", new state: 0x" << std::hex << new_state << std::dec << std::endl;

        // 发布原始状态事件（反应输出变化）
        auto now = std::chrono::high_resolution_clock::now();
        uint64_t ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
        publish_raw_state_event({new_state, ts_us});

        // 同时发布引脚变化事件
        publish_pin_event({pin, value, ts_us});
    } else {
        std::cerr << "[PCA9535 Service] Failed to set output pin " << static_cast<int>(pin) << std::endl;
    }
    return ok;
}

bool Pca9535Service::enable_servo_left(bool enable) {
    return set_output(Pca9535Pin::SRV_L_EN, enable);
}

bool Pca9535Service::enable_servo_right(bool enable) {
    return set_output(Pca9535Pin::SRV_R_EN, enable);
}

bool Pca9535Service::enable_servo_both(bool enable) {
    bool left_ok = enable_servo_left(enable);
    bool right_ok = enable_servo_right(enable);
    return left_ok && right_ok;
}

bool Pca9535Service::enable_tof(bool enable) {
    return set_output(Pca9535Pin::TOF_ENL, enable);
}

bool Pca9535Service::enable_cliff(bool enable) {
    // 悬崖传感器需要同时控制两个引脚：
    // 1. IRS_DRV (PCA9535 pin 15)
    // 2. IRS_EN (CM4 GPIO24) GPIO24不再作为IR使能，而是改为分默认使能，GPIO24配给编码器
    // bool irs_drv_ok = set_output(Pca9535Pin::IRS_DRV, enable);
    // bool irs_en_ok = hal_.set_irs_en(enable);
    return hal_.enable_cliff(enable);
}

bool Pca9535Service::set_ext_io(uint8_t io_num, bool value) {
    if (io_num > 5) {
        return false;
    }
    Pca9535Pin pin = static_cast<Pca9535Pin>(8 + io_num);  // EXT_IO_0 = 8
    return set_output(pin, value);
}

bool Pca9535Service::set_outputs_bulk(uint16_t state, uint16_t mask) {
    bool ok = hal_.write_bulk(state, mask);
    if (ok) {
        uint16_t old_state = current_state_.load();
        uint16_t new_state = (old_state & ~mask) | (state & mask);
        current_state_.store(new_state);

        std::cout << "[PCA9535 Service] Bulk output set: state=0x" << std::hex << state 
                  << ", mask=0x" << mask << ", new state: 0x" << new_state << std::dec << std::endl;

        // 发布原始状态事件
        auto now = std::chrono::high_resolution_clock::now();
        uint64_t ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
        publish_raw_state_event({new_state, ts_us});

        // 批量发布引脚变化事件
        for (uint8_t i = 0; i < 16; i++) {
            if (mask & (1u << i)) {
                publish_pin_event({static_cast<Pca9535Pin>(i), (bool)(state & (1u << i)), ts_us});
            }
        }
    } else {
        std::cerr << "[PCA9535 Service] Failed to set bulk output" << std::endl;
    }
    return ok;
}

uint64_t Pca9535Service::next_sub_id() {
    return next_sub_id_.fetch_add(1);
}

} // namespace extio
} // namespace doly
