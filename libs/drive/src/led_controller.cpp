/*

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/led_controller.hpp"
#include <iostream>
#include <cmath>
#include <chrono>

namespace {
constexpr uint32_t kDefaultBreathPeriodMs = 3000;
constexpr uint32_t kDefaultBlinkPeriodMs = 500;
constexpr uint32_t kMinEffectPeriodMs = 50;
}

namespace doly {
namespace drive {

LedController::LedController(const std::string& rgb_sequence)
    : rgb_sequence_(rgb_sequence) {
    // 转换为大写
    for (char& c : rgb_sequence_) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    // 确保序列有效
    if (rgb_sequence_.size() != 3 ||
        rgb_sequence_.find('R') == std::string::npos ||
        rgb_sequence_.find('G') == std::string::npos ||
        rgb_sequence_.find('B') == std::string::npos) {
        rgb_sequence_ = "RGB";  // fallback
    }
}

LedController::~LedController() {
    StopEffect();
    TurnOff();
}

bool LedController::Init() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        std::cout << "[LedController] Already initialized" << std::endl;
        return true;
    }
    
    std::cout << "[LedController] Initializing PWM channels for RGB LEDs..." << std::endl;
    
    // 初始化左臂 RGB PWM 通道
    if (GPIO::init(Pwm_Led_Left_R) != 0) {
        std::cerr << "[LedController] Failed to init Pwm_Led_Left_R" << std::endl;
        return false;
    }
    if (GPIO::init(Pwm_Led_Left_G) != 0) {
        std::cerr << "[LedController] Failed to init Pwm_Led_Left_G" << std::endl;
        return false;
    }
    if (GPIO::init(Pwm_Led_Left_B) != 0) {
        std::cerr << "[LedController] Failed to init Pwm_Led_Left_B" << std::endl;
        return false;
    }
    
    // 初始化右臂 RGB PWM 通道
    if (GPIO::init(Pwm_Led_Right_R) != 0) {
        std::cerr << "[LedController] Failed to init Pwm_Led_Right_R" << std::endl;
        return false;
    }
    if (GPIO::init(Pwm_Led_Right_G) != 0) {
        std::cerr << "[LedController] Failed to init Pwm_Led_Right_G" << std::endl;
        return false;
    }
    if (GPIO::init(Pwm_Led_Right_B) != 0) {
        std::cerr << "[LedController] Failed to init Pwm_Led_Right_B" << std::endl;
        return false;
    }
    
    initialized_ = true;
    std::cout << "[LedController] ✓ Initialized (PWM6-11 using Gpio API @ 50Hz)" << std::endl;
    
    // 初始状态：关闭
    TurnOff();
    
    return true;
}

void LedController::SetColor(uint8_t r, uint8_t g, uint8_t b) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        std::cerr << "[LedController] Not initialized" << std::endl;
        return;
    }
    
    // 停止正在运行的特效
    if (effect_running_.load()) {
        effect_running_.store(false);
        if (effect_thread_.joinable()) {
            effect_thread_.join();
        }
    }
    
    has_temporary_color_ = false; // Cancel any pending recovery

    current_effect_ = LED_SOLID;
    base_color_ = {r, g, b};
    current_color_ = {r, g, b};
    effect_period_ms_.store(0);
    
    ApplyRGB(r, g, b);
}

void LedController::SetColorTemporary(uint8_t r, uint8_t g, uint8_t b, uint32_t duration_ms, uint8_t default_r, uint8_t default_g, uint8_t default_b) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        std::cerr << "[LedController] Not initialized" << std::endl;
        return;
    }

    // 停止正在运行的特效
    if (effect_running_.load()) {
        effect_running_.store(false);
        if (effect_thread_.joinable()) {
            effect_thread_.join();
        }
    }

    // Apply target color
    current_effect_ = LED_SOLID;
    base_color_ = {r, g, b};
    current_color_ = {r, g, b};
    effect_period_ms_.store(0);
    ApplyRGB(r, g, b);

    // Setup recovery
    has_temporary_color_ = true;
    expiration_time_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);
    default_recovery_color_ = {default_r, default_g, default_b};
    
    std::cout << "[LedController] Set temporary color (" << (int)r << "," << (int)g << "," << (int)b 
              << ") for " << duration_ms << "ms, then revert to (" 
              << (int)default_r << "," << (int)default_g << "," << (int)default_b << ")" << std::endl;
}

void LedController::Loop() {
    if (!has_temporary_color_) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (has_temporary_color_ && std::chrono::steady_clock::now() >= expiration_time_) {
        has_temporary_color_ = false;
        
        // 停止正在运行的特效线程
        if (effect_running_.load()) {
            effect_running_.store(false);
            if (effect_thread_.joinable()) {
                effect_thread_.join();
            }
        }
        
        uint8_t r = default_recovery_color_[0];
        uint8_t g = default_recovery_color_[1];
        uint8_t b = default_recovery_color_[2];
        
        // Revert to default effect (SOLID color)
        current_effect_ = LED_SOLID;
        base_color_ = {r, g, b};
        current_color_ = {r, g, b};
        effect_period_ms_.store(0);
        ApplyRGB(r, g, b);
        
        std::cout << "[LedController] Auto-recovered to default color (" 
                  << (int)r << "," << (int)g << "," << (int)b << ")" << std::endl;
    }
}

void LedController::SetBrightness(uint8_t brightness) {
    std::lock_guard<std::mutex> lock(mutex_);
    brightness_ = brightness;
    // 如果当前是纯色模式，重新应用颜色以更新亮度
    if (current_effect_ == LED_SOLID) {
        ApplyRGB(current_color_[0], current_color_[1], current_color_[2]);
    }
}

void LedController::SetRgbOffsets(int8_t r_offset, int8_t g_offset, int8_t b_offset) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto clamp_offset = [](int8_t v) -> int8_t {
        if (v < -100) return -100;
        if (v > 100) return 100;
        return v;
    };
    r_offset_ = clamp_offset(r_offset);
    g_offset_ = clamp_offset(g_offset);
    b_offset_ = clamp_offset(b_offset);
    // 如果当前是纯色模式，重新应用颜色以更新补偿
    if (current_effect_ == LED_SOLID) {
        ApplyRGB(current_color_[0], current_color_[1], current_color_[2]);
    }
}

void LedController::ApplyRGB(uint8_t r, uint8_t g, uint8_t b) {
    // 应用亮度和补偿
    int rr = (static_cast<int>(r) * brightness_) / 255 + r_offset_;
    int gg = (static_cast<int>(g) * brightness_) / 255 + g_offset_;
    int bb = (static_cast<int>(b) * brightness_) / 255 + b_offset_;
    
    // clamp to 0-255
    auto clamp_255 = [](int v) -> int {
        if (v < 0) return 0;
        if (v > 255) return 255;
        return v;
    };
    rr = clamp_255(rr);
    gg = clamp_255(gg);
    bb = clamp_255(bb);
    
    uint8_t r_adj = static_cast<uint8_t>(rr);
    uint8_t g_adj = static_cast<uint8_t>(gg);
    uint8_t b_adj = static_cast<uint8_t>(bb);
    
    // 映射到 PWM 值（0-4095，12位）
    // 注意：硬件 PWM 逻辑为反向（0 对应最亮，4095 对应全关），所以需要反转
    constexpr uint16_t PWM_MAX = 4095;
    auto to_pwm = [&](uint8_t v) -> uint16_t {
        uint32_t val = (static_cast<uint32_t>(v) * PWM_MAX) / 255u;
        if (val > PWM_MAX) val = PWM_MAX;
        return static_cast<uint16_t>(PWM_MAX - val); // 反转逻辑
    };

    uint16_t pwm_vals[3] = {to_pwm(r_adj), to_pwm(g_adj), to_pwm(b_adj)};
    
    // 根据 rgb_sequence_ 映射到物理通道
    // rgb_sequence_[i] 告诉物理通道 i 应该写什么逻辑颜色
    auto get_pwm_for_channel = [&](int channel) -> uint16_t {
        if (channel >= 0 && channel < 3) {
            char ch = (channel < (int)rgb_sequence_.size()) ? rgb_sequence_[channel] : 'R';
            if (ch == 'R') return pwm_vals[0];
            else if (ch == 'G') return pwm_vals[1];
            else if (ch == 'B') return pwm_vals[2];
        }
        return pwm_vals[0];  // fallback
    };
    
    uint16_t left_r = get_pwm_for_channel(0);
    uint16_t left_g = get_pwm_for_channel(1);
    uint16_t left_b = get_pwm_for_channel(2);
    
    // 左臂 LED
    GPIO::writePwm(Pwm_Led_Left_R, left_r);
    GPIO::writePwm(Pwm_Led_Left_G, left_g);
    GPIO::writePwm(Pwm_Led_Left_B, left_b);
    
    // 右臂 LED (假设相同接线)
    GPIO::writePwm(Pwm_Led_Right_R, left_r);
    GPIO::writePwm(Pwm_Led_Right_G, left_g);
    GPIO::writePwm(Pwm_Led_Right_B, left_b);
    
    current_color_ = {r, g, b};
}

uint32_t LedController::ResolveEffectPeriodMs(LedEffect effect, float frequency_hz) const {
    uint32_t default_period_ms = (effect == LED_BLINK) ? kDefaultBlinkPeriodMs : kDefaultBreathPeriodMs;
    if (!(frequency_hz > 0.0f) || !std::isfinite(frequency_hz)) {
        return default_period_ms;
    }

    double period_ms = 1000.0 / static_cast<double>(frequency_hz);
    if (!(period_ms > 0.0)) {
        return default_period_ms;
    }

    uint32_t resolved = static_cast<uint32_t>(std::lround(period_ms));
    if (resolved < kMinEffectPeriodMs) {
        resolved = kMinEffectPeriodMs;
    }
    return resolved;
}

void LedController::StartEffect(LedEffect effect, std::array<uint8_t, 3> color, uint32_t duration_ms, float frequency_hz) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        std::cerr << "[LedController] Not initialized" << std::endl;
        return;
    }
    
    // 停止之前的特效
    if (effect_running_.load()) {
        effect_running_.store(false);
        if (effect_thread_.joinable()) {
            effect_thread_.join();
        }
    }
    
    // 如果有超时 duration 且不是 0，设置超时恢复
    if (duration_ms > 0) {
        has_temporary_color_ = true;
        expiration_time_ = std::chrono::steady_clock::now() + std::chrono::milliseconds(duration_ms);
        // 超时后恢复到默认颜色（黑色/关闭）
        default_recovery_color_ = {0, 0, 0};
        
        std::cout << "[LedController] StartEffect(effect=" << effect << ", duration=" << duration_ms 
                  << "ms) - auto stop after timeout" << std::endl;
    }
    
    current_effect_ = effect;
    base_color_ = color;
    effect_period_ms_.store(ResolveEffectPeriodMs(effect, frequency_hz));
    
    if (effect == LED_SOLID) {
        ApplyRGB(color[0], color[1], color[2]);
    } else if (effect == LED_OFF) {
        TurnOff();
    } else {
        // 启动特效线程
        effect_running_.store(true);
        effect_thread_ = std::thread(&LedController::EffectThreadFunc, this);
    }
}

void LedController::StartEffectTemporary(LedEffect effect, std::array<uint8_t, 3> color, 
                                          uint32_t duration_ms, uint32_t hold_duration, 
                                          std::array<uint8_t, 3> recovery_color, float frequency_hz) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_) {
        std::cerr << "[LedController] Not initialized" << std::endl;
        return;
    }
    
    // 停止之前的特效
    if (effect_running_.load()) {
        effect_running_.store(false);
        if (effect_thread_.joinable()) {
            effect_thread_.join();
        }
    }
    
    // 设置超时恢复机制
    has_temporary_color_ = true;
    expiration_time_ = std::chrono::steady_clock::now() + 
                       std::chrono::milliseconds(duration_ms + hold_duration);
    default_recovery_color_ = recovery_color;
    
    std::cout << "[LedController] StartEffectTemporary(effect=" << effect 
              << ", duration=" << duration_ms << "ms, hold=" << hold_duration << "ms)" << std::endl;
    
    current_effect_ = effect;
    base_color_ = color;
    effect_period_ms_.store(ResolveEffectPeriodMs(effect, frequency_hz));
    
    if (effect == LED_SOLID) {
        ApplyRGB(color[0], color[1], color[2]);
    } else if (effect == LED_OFF) {
        TurnOff();
    } else {
        // 启动特效线程
        effect_running_.store(true);
        effect_thread_ = std::thread(&LedController::EffectThreadFunc, this);
    }
}

void LedController::StopEffect() {
    if (effect_running_.load()) {
        effect_running_.store(false);
        if (effect_thread_.joinable()) {
            effect_thread_.join();
        }
    }
}

void LedController::TurnOff() {
    ApplyRGB(0, 0, 0);
    current_effect_ = LED_OFF;
    effect_period_ms_.store(0);
}

std::array<uint8_t, 3> LedController::GetCurrentColor() const {
    return current_color_;
}

void LedController::EffectThreadFunc() {
    auto start_time = std::chrono::steady_clock::now();
    
    while (effect_running_.load()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();
        
        if (current_effect_ == LED_BREATH) {
            UpdateBreathEffect(elapsed);
        } else if (current_effect_ == LED_BLINK) {
            UpdateBlinkEffect(elapsed);
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(20));  // 50Hz 更新
    }
}

void LedController::UpdateBreathEffect(uint64_t time_ms) {
    uint32_t period_ms = effect_period_ms_.load();
    if (period_ms < kMinEffectPeriodMs) {
        period_ms = kDefaultBreathPeriodMs;
    }

    float phase = static_cast<float>(time_ms % period_ms) / static_cast<float>(period_ms);
    float brightness = (std::sin(phase * 2 * M_PI - M_PI / 2) + 1.0f) / 2.0f;  // 0.0 - 1.0
    
    uint8_t r = static_cast<uint8_t>(base_color_[0] * brightness);
    uint8_t g = static_cast<uint8_t>(base_color_[1] * brightness);
    uint8_t b = static_cast<uint8_t>(base_color_[2] * brightness);
    
    ApplyRGB(r, g, b);
}

void LedController::UpdateBlinkEffect(uint64_t time_ms) {
    uint32_t period_ms = effect_period_ms_.load();
    if (period_ms < kMinEffectPeriodMs) {
        period_ms = kDefaultBlinkPeriodMs;
    }

    bool on = (time_ms % period_ms) < (period_ms / 2u);
    
    if (on) {
        ApplyRGB(base_color_[0], base_color_[1], base_color_[2]);
    } else {
        ApplyRGB(0, 0, 0);
    }
}

} // namespace drive
} // namespace doly
