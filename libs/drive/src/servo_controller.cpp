/*

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/servo_controller.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>
#include <string>
#include <cmath>
#include <algorithm>

namespace doly {
namespace drive {

// Easing functions
static float EaseInOutSine(float t) {
    return -(std::cos(3.14159f * t) - 1.0f) / 2.0f;
}

static float Clamp(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

// 析构
ServoController::~ServoController() {
    running_ = false;
    if (loop_thread_.joinable()) {
        loop_thread_.join();
    }
}


void ServoController::LoadConfig() {
    // 默认值
    int left_min_us = 500;
    int left_max_us = 2500;
    int left_max_angle = SERVO_ARM_MAX_ANGLE;
    bool invert_left = false;

    int right_min_us = 500;
    int right_max_us = 2500;
    int right_max_angle = SERVO_ARM_MAX_ANGLE;
    bool invert_right = true;

    const std::string cfg_path = "/home/pi/dolydev/config/servo_offsets.cfg";
    std::ifstream ifs(cfg_path);
    if (ifs.is_open()) {
        std::string line;
        while (std::getline(ifs, line)) {
            if (line.empty() || line[0] == '#') continue;
            auto pos = line.find('=');
            if (pos == std::string::npos) continue;
            std::string key = line.substr(0, pos);
            std::string val = line.substr(pos + 1);
            
            auto trim = [](std::string &s){
                size_t a = s.find_first_not_of(" \t\r\n");
                size_t b = s.find_last_not_of(" \t\r\n");
                if (a==std::string::npos) { s.clear(); return; }
                s = s.substr(a, b-a+1);
            };
            trim(key); trim(val);
            
            try {
                double f = std::stod(val);
                if (key == "left") left_offset_ = static_cast<float>(f);
                else if (key == "right") right_offset_ = static_cast<float>(f);
                else if (key == "center_offset_left") center_offset_left_ = static_cast<float>(f);
                else if (key == "center_offset_right") center_offset_right_ = static_cast<float>(f);
                else if (key == "left_min_us") left_min_us = static_cast<int>(f);
                else if (key == "left_max_us") left_max_us = static_cast<int>(f);
                else if (key == "left_max_angle") { left_max_angle = static_cast<int>(f); left_max_angle_ = left_max_angle; }
                else if (key == "right_min_us") right_min_us = static_cast<int>(f);
                else if (key == "right_max_us") right_max_us = static_cast<int>(f);
                else if (key == "right_max_angle") { right_max_angle = static_cast<int>(f); right_max_angle_ = right_max_angle; }
                else if (key == "invert_left" || key == "left_invert") invert_left = (f != 0.0);
                else if (key == "invert_right" || key == "right_invert") invert_right = (f != 0.0);
            } catch (...) {}
        }
        std::cout << "[ServoController] Loaded config: L_off=" << left_offset_ << " R_off=" << right_offset_ << std::endl;
    }

    // Init hardware via HAL
    ServoMotor::Init();
    ServoMotor::setup(SERVO_LEFT, left_min_us, left_max_us, left_max_angle, invert_left);
    ServoMotor::setup(SERVO_RIGHT, right_min_us, right_max_us, right_max_angle, invert_right);
}

bool ServoController::Init(const std::map<ServoChannel, float>& initial_angles,
                          const std::map<ServoChannel, bool>& auto_hold_modes) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) return true;
    std::cout << "[ServoController] Initializing with P1.3 Smooth Startup support..." << std::endl;

    LoadConfig();

    // 初始化状态
    {
        std::lock_guard<std::mutex> sl(state_mutex_);
        
        // 设置初始角度
        for (auto const& [ch, angle] : initial_angles) {
            states_[ch].current_angle = angle;
            states_[ch].target_angle = angle;
            states_[ch].active = false;
            
            // 重要：在开启物理电源之前，先写入一次 PWM 脉宽
            // 这样当 PCA9535 闭合电源继电器时，舵机已经有正确的波形信号，不会发生 Jump
            WriteHardware(ch, angle, 50);
            
            // P1.3 修改：即使已经写入了 PWM，我们依然选择在初始化时不自动上电
            // 而是等待第一条运动指令到来时再上电（Auto-Wake），以彻底消除启动时的抖动风险
            // if (power_cb_) {
            //    power_cb_(ch, true);
            // }
        }

        // 设置自动保持模式
        for (auto const& [ch, en] : auto_hold_modes) {
            states_[ch].auto_hold_enabled = en;
        }
    }

    // 启动线程
    running_ = true;
    loop_thread_ = std::thread(&ServoController::Loop, this);

    initialized_ = true;
    return true;
}

void ServoController::WriteHardware(ServoChannel channel, float angle, uint8_t speed) {
    if (angle < 0.0f) angle = 0.0f;
    if (angle > 180.0f) angle = 180.0f;
    
    float mapped_angle = angle;
    float max_angle = (channel == SERVO_LEFT) ? left_max_angle_ : right_max_angle_;
    
    // 角度映射：将0-180度映射到0-max_angle度
    mapped_angle = (angle / 180.0f) * max_angle;
    
    // 应用角度偏移量
    if (channel == SERVO_LEFT) {
        mapped_angle += left_offset_;
    } else if (channel == SERVO_RIGHT) {
        mapped_angle += right_offset_;
    }
    
    // 确保角度在有效范围内
    if (mapped_angle < 0.0f) mapped_angle = 0.0f;
    if (mapped_angle > max_angle) mapped_angle = max_angle;
    
    // 使用最大速度 (0) 因为我们自己在做插值
    ServoMotor::set(channel, mapped_angle, speed); 
}

void ServoController::Loop() {
    while (running_) {
        auto now = std::chrono::steady_clock::now();
        auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();

        {
            std::lock_guard<std::mutex> lock(state_mutex_);
            for (auto& pair : states_) {
                ServoChannel ch = pair.first;
                ServoState& s = pair.second;

                if (!s.active) {
                    // 检查是否需要自动释放舵机
                    if (s.auto_hold_enabled && s.auto_hold_duration_ms > 0 && s.motion_complete_time_ms > 0) {
                        long long elapsed_since_complete = now_ms - s.motion_complete_time_ms;
                        if (elapsed_since_complete >= s.auto_hold_duration_ms) {
                            // 1. 释放舵机使能（这种方式无效）要使用enable_servo_left
                            ServoMotor::stop(ch);
                            // 2. 触发回调以关闭物理电源 (Pca9535 GPIO)
                            if (power_cb_) {
                                power_cb_(ch, false);
                            }

                            std::cout << "[ServoController] Auto-release(PWM+Power) servo " << (ch == SERVO_LEFT ? "LEFT" : "RIGHT")
                                      << " after " << s.auto_hold_duration_ms << "ms hold" << std::endl;
                            s.motion_complete_time_ms = 0;  // 重置，避免重复释放
                        }
                    }
                    continue;
                }

                float t = 0.0f;
                bool finished = false;

                if (s.duration_ms <= 0) {
                    t = 1.0f;
                    finished = true;
                } else {
                    long long elapsed = now_ms - s.start_time_ms;
                    if (elapsed >= s.duration_ms) {
                        t = 1.0f;
                        finished = true;
                    } else {
                        t = (float)elapsed / (float)s.duration_ms;
                    }
                }

                // Apply easing
                float ease_t = t;
                if (s.easing == EasingType::EaseInOutSine) {
                    ease_t = EaseInOutSine(t);
                }

                s.current_angle = s.start_angle + (s.target_angle - s.start_angle) * ease_t;
                WriteHardware(ch, s.current_angle, s.speed);

                if (finished) {
                    if (s.swing_after_move) {
                        s.swing_after_move = false;
                        if (s.swing_pending_amplitude > 0.0f) {
                            float center = Clamp(s.swing_pending_center, 0.0f, 180.0f);
                            float half = Clamp(s.swing_pending_amplitude, 0.0f, 180.0f);
                            float min_angle = Clamp(center - half, 0.0f, 180.0f);
                            float max_angle = Clamp(center + half, 0.0f, 180.0f);
                            if (min_angle > max_angle) std::swap(min_angle, max_angle);
                            float range = max_angle - min_angle;
                            if (range > 0.0f) {
                                int swing_duration = DurationForSpeed(range, s.swing_pending_speed);
                                if (swing_duration <= 0) swing_duration = 200;
                                s.swing_min = min_angle;
                                s.swing_max = max_angle;
                                s.swing_count = s.swing_pending_count;
                                s.speed = s.swing_pending_speed;
                                s.start_angle = s.current_angle;
                                if (s.current_angle <= center) {
                                    s.swing_direction = 1;
                                    s.target_angle = max_angle;
                                } else {
                                    s.swing_direction = -1;
                                    s.target_angle = min_angle;
                                }
                                s.start_time_ms = now_ms;
                                s.duration_ms = swing_duration;
                                s.easing = EasingType::EaseInOutSine;
                                s.is_swinging = true;
                                s.active = true;
                                continue;
                            }
                        }
                        s.active = false;
                        s.motion_complete_time_ms = now_ms;  // 记录完成时间
                        continue;
                    }
                    if (s.is_swinging) {
                        // Swing logic
                        if (s.swing_count != 0) {
                            // Toggle direction
                            s.start_angle = s.current_angle;
                            s.start_time_ms = now_ms;
                            
                            if (s.swing_direction > 0) {
                                // max reached, go to min
                                s.target_angle = s.swing_min;
                                s.swing_direction = -1;
                            } else {
                                // min reached, go to max
                                s.target_angle = s.swing_max;
                                s.swing_direction = 1;
                            }

                            if (s.swing_count > 0) {
                                s.swing_count--;
                            }
                        } else {
                            s.active = false;
                            s.is_swinging = false;
                            s.motion_complete_time_ms = now_ms;  // 记录完成时间
                        }
                    } else {
                        s.active = false;
                        s.current_angle = s.target_angle; // Ensure exact final
                        WriteHardware(ch, s.current_angle, s.speed);
                        s.motion_complete_time_ms = now_ms;  // 记录完成时间
                    }
                }
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20)); // 50Hz
    }
}


// 兼容旧接口
bool ServoController::SetAngle(ServoChannel channel, float angle, uint8_t speed) {
    // 简单映射 speed (0-100) -> duration
    // speed 100 -> 300ms for 180 deg?
    // speed 1 -> 3000ms?
    // 旧逻辑: speed=0 means fast? No parameter is 50.
    
    // 为了兼容，使用 MoveMultiDuration 
    // 简单计算：假定全速(100) 是 60deg/0.15s => 400deg/s
    // speed 50 => 200deg/s
    
    if (!initialized_) return false;

    float current = 90.0f;
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        current = states_[channel].current_angle;
    }
    
    float diff = std::abs(angle - current);
    
    // 如果 speed = 100 or 0, 视为最快
    int duration = 0;
    if (speed > 0 && speed < 100 && diff > 1.0f) {
        // 映射 speed 1-99 到速度因子
        // 假设 speed=50 对应 0.5 deg/ms (2ms/deg)
        // 100 = fast, 1 = slow
        float ms_per_deg = 20.0f / (float)speed * 2.0f; // heuristic
        duration = (int)(diff * ms_per_deg);
    }
    
    std::map<ServoChannel, float> targets;
    targets[channel] = angle;
    MoveMultiDuration(targets, duration);
    return true;
}

bool ServoController::SetAngleSmooth(ServoChannel channel, float angle, int duration_ms) {
    std::map<ServoChannel, float> targets;
    targets[channel] = angle;
    MoveMultiDuration(targets, duration_ms);
    return true;
}

void ServoController::MoveMultiDuration(const std::map<ServoChannel, float>& targets, int duration_ms) {
    auto now = std::chrono::steady_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();

    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (auto const& [ch, target] : targets) {
            ServoState& s = states_[ch];
            
            // P1.3 新逻辑: 每当有新动作产生，确保物理电源已开启
            if (power_cb_) {
                power_cb_(ch, true);
            }

            s.start_angle = s.current_angle;
            s.target_angle = target;
            s.start_time_ms = now_ms;
            s.duration_ms = duration_ms;
            s.easing = EasingType::EaseInOutSine;
            s.active = true;
            s.is_swinging = false;
            s.swing_after_move = false;
            s.motion_complete_time_ms = 0; // 重置计时，防止还在运动就触发自动释放
        }
    }
}

void ServoController::MoveMulti(const std::map<ServoChannel, float>& targets, uint8_t speed_val) {
    if (targets.empty()) return;
    
    // 找出最大移动幅度
    float max_diff = 0;
    
    {
        std::lock_guard<std::mutex> lock(state_mutex_);
        for (const auto& kv : targets) {
            float diff = std::abs(states_[kv.first].current_angle - kv.second);
            if (diff > max_diff) max_diff = diff;
        }
    }
    
    // 计算持续时间
    int duration = 0;
    if (speed_val < 100.0f && speed_val > 0.0f) {
        float ms_per_deg = 40.0f / speed_val * 2.5f; // Adjust magic number
        duration = (int)(max_diff * ms_per_deg);
    }
    
    MoveMultiDuration(targets, duration);
}

int ServoController::DurationForSpeed(float angle_diff, uint8_t speed) const {
    if (angle_diff <= 0.0f || speed == 0) return 0;
    float ms_per_deg = 40.0f / static_cast<float>(speed) * 2.5f;
    return static_cast<int>(angle_diff * ms_per_deg);
}

// 先移动到目标角度，再围绕原始角度执行指定幅度的摆动
void ServoController::ServoSwingOf(ServoChannel channel, float target_angle, uint8_t approach_speed, float swing_amplitude, uint8_t swing_speed, int count) {
    auto now = std::chrono::steady_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();

    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // P1.3 新逻辑: 每当有新动作产生，确保物理电源已开启
    if (power_cb_) {
        power_cb_(channel, true);
    }

    ServoState& s = states_[channel];
    float prev_angle = s.current_angle;
    uint8_t safe_approach_speed = (approach_speed == 0 ? 50 : approach_speed);
    uint8_t safe_swing_speed = (swing_speed == 0 ? 50 : swing_speed);
    float amplitude = std::abs(swing_amplitude);
    amplitude = Clamp(amplitude, 0.0f, 180.0f);

    int duration_ms = DurationForSpeed(std::abs(prev_angle - target_angle), safe_approach_speed);
    if (duration_ms <= 0) duration_ms = 200;

    s.start_angle = prev_angle;
    s.target_angle = Clamp(target_angle, 0.0f, 180.0f);
    s.start_time_ms = now_ms;
    s.duration_ms = duration_ms;
    s.easing = (duration_ms > 200) ? EasingType::EaseInOutSine : EasingType::Linear;
    s.speed = safe_approach_speed;
    s.active = true;
    s.is_swinging = false;
    s.swing_after_move = true;
    s.swing_pending_center = s.target_angle;
    s.swing_pending_amplitude = amplitude;
    s.swing_pending_speed = safe_swing_speed;
    s.swing_pending_count = count;
}

void ServoController::StartSwing(ServoChannel channel, float min_angle, float max_angle, int duration_one_way, int count) {
    auto now = std::chrono::steady_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now).time_since_epoch().count();

    std::lock_guard<std::mutex> lock(state_mutex_);
    
    // P1.3 新逻辑: 每当有新动作产生，确保物理电源已开启
    if (power_cb_) {
        power_cb_(channel, true);
    }
    
    ServoState& s = states_[channel];
    
    // 初始化摆动
    // 先移动到 max (或 min?)
    // 逻辑：current -> max -> min -> max ...
    
    s.swing_min = min_angle;
    s.swing_max = max_angle;
    s.swing_count = count; // 次数
    s.swing_direction = 1; // towards max
    
    s.start_angle = s.current_angle;
    s.target_angle = max_angle;
    
    // 初次移动也使用同样速度
    s.start_time_ms = now_ms;
    s.duration_ms = duration_one_way; // approx
    s.easing = EasingType::EaseInOutSine;
    s.is_swinging = true;
    s.active = true;
}

bool ServoController::Stop(ServoChannel channel) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    states_[channel].active = false;
    states_[channel].is_swinging = false;
    states_[channel].motion_complete_time_ms = 0;
    
    // 停止 PWM 信号
    ServoMotor::stop(channel); 
    // 触发电源回调关闭舵机电源 (Pca9535 IO)
    if (power_cb_) {
        power_cb_(channel, false);
    }
    return true;
}

void ServoController::StopAll() {
    Stop(SERVO_LEFT);
    Stop(SERVO_RIGHT);
}

void ServoController::SetAutoHold(ServoChannel channel, bool enabled, long long hold_duration_ms) {
    std::lock_guard<std::mutex> lock(state_mutex_);
    states_[channel].auto_hold_enabled = enabled;
    states_[channel].auto_hold_duration_ms = hold_duration_ms;
    std::cout << "[ServoController] SetAutoHold: ch=" << (channel == SERVO_LEFT ? "LEFT" : "RIGHT")
              << " enabled=" << (enabled ? "true" : "false")
              << " duration=" << hold_duration_ms << "ms" << std::endl;
}

void ServoController::SetPowerCallback(ServoPowerCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    power_cb_ = cb;
}

bool ServoController::IsActive() const {
    return ServoMotor::isActive();
}

// ========== 新增动作函数实现 ==========

void ServoController::LiftDumbbell(ServoChannel channel, float weight, int reps) {
    // 重量映射: 0-100 → 速度 (轻=快, 重=慢)
    float speed = 80.0f - (weight * 0.6f);
    if (speed < 20.0f) speed = 20.0f;
    if (speed > 80.0f) speed = 80.0f;
    
    // 重量映射: 幅度 (轻=大幅度90°, 重=小幅度30°)
    float amplitude = 90.0f - (weight * 0.6f);
    if (amplitude < 30.0f) amplitude = 30.0f;
    
    float down_angle = 90.0f;  // 初始位置
    float up_angle = down_angle - amplitude;  // 举起位置
    
    for (int i = 0; i < reps; ++i) {
        SetAngleSmooth(channel, up_angle, static_cast<int>(1000.0f / speed));  // 举起
        std::this_thread::sleep_for(std::chrono::milliseconds(static_cast<int>(300 * (1 + weight / 100.0f))));
        SetAngleSmooth(channel, down_angle, static_cast<int>(800.0f / speed)); // 放下
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void ServoController::DumbbellDance(float weight, float duration_sec) {
    auto start_time = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration<float>(duration_sec);
    
    int rep_count = 0;
    while (std::chrono::steady_clock::now() - start_time < duration) {
        // 左右手交替
        if (rep_count % 2 == 0) {
            LiftDumbbell(SERVO_LEFT, weight, 1);
        } else {
            LiftDumbbell(SERVO_RIGHT, weight, 1);
        }
        rep_count++;
    }
}

void ServoController::WaveFlag(ServoChannel channel, float flag_weight, int wave_count) {
    float speed = 80.0f - (flag_weight * 0.5f);
    if (speed < 30.0f) speed = 30.0f;
    
    float amplitude = 120.0f - (flag_weight * 0.8f);  // 轻旗子大幅度120°, 重旗子小幅度40°
    if (amplitude < 40.0f) amplitude = 40.0f;
    
    float center = 90.0f;
    float left_angle = center - amplitude / 2;
    float right_angle = center + amplitude / 2;
    
    for (int i = 0; i < wave_count; ++i) {
        SetAngleSmooth(channel, left_angle, static_cast<int>(500.0f / speed));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        SetAngleSmooth(channel, right_angle, static_cast<int>(500.0f / speed));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void ServoController::BeatDrum(ServoChannel channel, float stick_weight, int beat_count) {
    float speed = 90.0f - (stick_weight * 0.7f);
    if (speed < 40.0f) speed = 40.0f;
    
    float strike_amplitude = 60.0f - (stick_weight * 0.4f);  // 重鼓棒幅度小20°, 轻鼓棒60°
    if (strike_amplitude < 20.0f) strike_amplitude = 20.0f;
    
    float ready_angle = 90.0f;
    float strike_angle = ready_angle + strike_amplitude;
    
    for (int i = 0; i < beat_count; ++i) {
        SetAngleSmooth(channel, strike_angle, static_cast<int>(300.0f / speed));  // 击打
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        SetAngleSmooth(channel, ready_angle, static_cast<int>(200.0f / speed));   // 回位
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void ServoController::PaddleRow(ServoChannel channel, float paddle_weight, int stroke_count) {
    // 拉桨(慢/强): 速度慢, 幅度大
    float pull_speed = 30.0f + (paddle_weight * 0.3f);  // 越重越慢
    if (pull_speed > 60.0f) pull_speed = 60.0f;
    
    // 回桨(快/轻): 速度快, 幅度一致
    
    float stroke_amplitude = 100.0f - (paddle_weight * 0.6f);  // 重桨幅度小40°, 轻桨100°
    if (stroke_amplitude < 40.0f) stroke_amplitude = 40.0f;
    
    // Fix: 定义回桨速度
    float return_speed = 70.0f - (paddle_weight * 0.2f);
    if (return_speed < 50.0f) return_speed = 50.0f;

    float start_angle = 90.0f;
    float pull_angle = start_angle - stroke_amplitude;
    
    for (int i = 0; i < stroke_count; ++i) {
        SetAngleSmooth(channel, pull_angle, static_cast<int>(1500.0f / pull_speed));   // 拉桨(慢)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        SetAngleSmooth(channel, start_angle, static_cast<int>(800.0f / return_speed)); // 回桨(快)
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

void ServoController::DualPaddleRow(float paddle_weight, int stroke_count) {
    float pull_speed = 30.0f + (paddle_weight * 0.3f);
    if (pull_speed > 60.0f) pull_speed = 60.0f;
    
    float return_speed = 70.0f - (paddle_weight * 0.2f);
    if (return_speed < 50.0f) return_speed = 50.0f;
    
    float stroke_amplitude = 100.0f - (paddle_weight * 0.6f);
    if (stroke_amplitude < 40.0f) stroke_amplitude = 40.0f;
    
    float start_angle = 90.0f;
    float pull_angle = start_angle - stroke_amplitude;
    
    int pull_duration = static_cast<int>(1500.0f / pull_speed);
    int return_duration = static_cast<int>(800.0f / return_speed);
    
    for (int i = 0; i < stroke_count; ++i) {
        // 双臂同步拉桨
        SetAngleSmooth(SERVO_LEFT, pull_angle, pull_duration);
        SetAngleSmooth(SERVO_RIGHT, pull_angle, pull_duration);
        std::this_thread::sleep_for(std::chrono::milliseconds(pull_duration + 100));
        
        // 双臂同步回桨
        SetAngleSmooth(SERVO_LEFT, start_angle, return_duration);
        SetAngleSmooth(SERVO_RIGHT, start_angle, return_duration);
        std::this_thread::sleep_for(std::chrono::milliseconds(return_duration + 100));
    }
}

} // namespace drive
} // namespace doly

