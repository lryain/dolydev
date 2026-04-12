#pragma once
#include "Gpio.h"  // 官方 Doly API
#include <array>
#include <cstdint>
#include <mutex>
#include <thread>
#include <atomic>

namespace doly {
namespace drive {

/**
 * @brief LED 特效类型
 */
enum LedEffect {
    LED_OFF,        // 关闭
    LED_SOLID,      // 纯色
    LED_BREATH,     // 呼吸（默认3秒周期，可配置频率）
    LED_BLINK,      // 闪烁（默认500ms，可配置频率）
    LED_FADE,       // 渐变
};

/**
 * @brief RGB LED 控制器（薄包装官方 Gpio PWM API）
 * 
 * 控制左右手臂的 RGB LED（PWM6-11）
 * 支持纯色显示和特效（呼吸、闪烁等）
 */
class LedController {
public:
    LedController(const std::string& rgb_sequence = "RGB");
    ~LedController();
    
    /**
     * @brief 初始化 LED 模块
     * 
     * 初始化 PWM6-11（左右手臂 RGB）
     * 
     * @return true 成功，false 失败
     */
    bool Init();
    
    /**
     * @brief 设置颜色（永久）
     * 
     * @param r, g, b RGB颜色值（0-255）
     */
    void SetColor(uint8_t r, uint8_t g, uint8_t b);

    /**
     * @brief 设置颜色（带超时自动恢复）
     * 
     * @param r, g, b 目标颜色
     * @param duration_ms 保持时长（毫秒）
     * @param default_r, default_g, default_b 恢复后的默认颜色
     */
    void SetColorTemporary(uint8_t r, uint8_t g, uint8_t b, uint32_t duration_ms, uint8_t default_r, uint8_t default_g, uint8_t default_b);
    
    /**
     * @brief 主循环，处理超时恢复
     */
    void Loop();

    /**
     * @brief 设置亮度
     * 
     * @param brightness 亮度值（0-255）
     */
    void SetBrightness(uint8_t brightness);
    
    /**
     * @brief 设置RGB通道补偿
     * 
     * @param r_offset, g_offset, b_offset 补偿值（-30 到 +30）
     */
    void SetRgbOffsets(int8_t r_offset, int8_t g_offset, int8_t b_offset);
    
    /**
     * @brief 启动特效
     * 
     * @param effect 特效类型
     * @param color 基础颜色（RGB，默认白色）
     * @param duration_ms 特效持续时间（0表示无限）
     */
    void StartEffect(LedEffect effect, std::array<uint8_t, 3> color = {255, 255, 255}, uint32_t duration_ms = 0, float frequency_hz = 0.0f);
    
    /**
     * @brief 启动特效（带超时自动恢复）
     * 
     * @param effect 特效类型
     * @param color 目标颜色
     * @param duration_ms 特效持续时间
     * @param hold_duration 恢复后保持时长
     * @param recovery_color 恢复的默认颜色
     */
    void StartEffectTemporary(LedEffect effect, std::array<uint8_t, 3> color, uint32_t duration_ms, uint32_t hold_duration, std::array<uint8_t, 3> recovery_color, float frequency_hz = 0.0f);
    
    /**
     * @brief 停止特效
     */
    void StopEffect();
    
    /**
     * @brief 关闭所有 LED
     */
    void TurnOff();
    
    /**
     * @brief 获取当前颜色
     */
    std::array<uint8_t, 3> GetCurrentColor() const;
    
private:
    bool initialized_ = false;
    LedEffect current_effect_ = LED_OFF;
    std::array<uint8_t, 3> base_color_ = {0, 0, 0};
    std::array<uint8_t, 3> current_color_ = {0, 0, 0};
    std::string rgb_sequence_;
    uint8_t brightness_ = 255;
    int8_t r_offset_ = 0, g_offset_ = 0, b_offset_ = 0;
    
    std::atomic<bool> effect_running_{false};
    std::thread effect_thread_;
    std::mutex mutex_;
    std::atomic<uint32_t> effect_period_ms_{0};

    // P1.4 Auto-recovery
    std::chrono::steady_clock::time_point expiration_time_;
    bool has_temporary_color_ = false;
    std::array<uint8_t, 3> default_recovery_color_ = {0, 0, 0};
    
    /**
     * @brief 应用 RGB 颜色到硬件
     */
    void ApplyRGB(uint8_t r, uint8_t g, uint8_t b);
    
    /**
     * @brief 特效线程函数
     */
    void EffectThreadFunc();
    
    /**
     * @brief 更新呼吸效果
     */
    void UpdateBreathEffect(uint64_t time_ms);
    
    /**
     * @brief 更新闪烁效果
     */
    void UpdateBlinkEffect(uint64_t time_ms);

    /**
     * @brief 根据频率解析呼吸/闪烁周期
     */
    uint32_t ResolveEffectPeriodMs(LedEffect effect, float frequency_hz) const;
};

} // namespace drive
} // namespace doly
