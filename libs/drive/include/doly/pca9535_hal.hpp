/**
 * @file pca9535_hal.hpp
 * @brief PCA9535 HAL 层（基于 libgpiod bulk read）
 * 
 * PCA9535 通过设备树映射为 gpiochip2
 * 使用 libgpiod 批量读取 GPIO 状态
 */

#pragma once

#include <gpiod.h>
#include <cstdint>
#include <string>
#include <array>

namespace doly {
namespace extio {

/**
 * @brief PCA9535 引脚枚举
 */
enum class Pca9535Pin : uint8_t {
    // Port 0 (0-7)
    IRS_FL = 0,      ///< 前左悬崖传感器
    IRS_FR = 1,      ///< 前右悬崖传感器
    IRS_BL = 2,      ///< 后左悬崖传感器
    IRS_BR = 3,      ///< 后右悬崖传感器
    TOUCH_R = 4,     ///< 右触摸传感器
    TOUCH_L = 5,     ///< 左触摸传感器
    SRV_L_EN = 6,    ///< 左舵机使能
    SRV_R_EN = 7,    ///< 右舵机使能
    
    // Port 1 (8-15)
    EXT_IO_0 = 8,    ///< 扩展 IO 0
    EXT_IO_1 = 9,    ///< 扩展 IO 1
    EXT_IO_2 = 10,   ///< 扩展 IO 2
    EXT_IO_3 = 11,   ///< 扩展 IO 3
    EXT_IO_4 = 12,   ///< 扩展 IO 4
    EXT_IO_5 = 13,   ///< 扩展 IO 5

    TOF_ENL = 14,    ///< TOF 使能
    IRS_DRV = 15     ///< 悬崖 IR 使能
};

/**
 * @brief PCA9535 HAL 层
 * 
 * 功能：
 * - Bulk read 16-bit GPIO 状态
 * - 单个 GPIO 读写
 * - 中断监听（GPIO1）
 */
class Pca9535Hal {
public:
    Pca9535Hal() = default;
    ~Pca9535Hal();

    /**
     * @brief 初始化
     * @param chip_name PCA9535 gpiochip 名称（默认 "gpiochip2"）
     * @param irq_chip_name 中断 GPIO chip（默认 "gpiochip0"）
     * @param irq_line 中断 GPIO 线号（默认 1）
     * @param irs_en_line 悬崖传感器使能 GPIO 线号（CM4 GPIO24）
     */
    bool init(const char* chip_name = "gpiochip2",
              const char* irq_chip_name = "gpiochip0",
              unsigned int irq_line = 1);

    /**
     * @brief Bulk read 所有 GPIO 状态
     * @return 16-bit 状态（bit0=pin0, bit15=pin15）
     */
    uint16_t bulk_read();

    /**
     * @brief 读取单个引脚
     * @param pin 引脚枚举
     * @return 电平状态
     */
    bool read_pin(Pca9535Pin pin);

    /**
     * @brief 写入单个引脚
     * @param pin 引脚枚举
     * @param value 电平状态
     * @return 成功返回 true
     */
    bool write_pin(Pca9535Pin pin, bool value);

    /**
     * @brief 批量设置输出引脚状态
     * @param state 状态字（16-bit）
     * @param mask 掩码（1=要修改的引脚，0=保持不变）
     * @return 成功返回 true
     */
    bool write_bulk(uint16_t state, uint16_t mask);

    /**
     * @brief 等待中断（阻塞）
     * @param timeout_ns 超时（纳秒），nullptr 表示永久等待
     * @return true=收到中断，false=超时/错误
     */
    bool wait_interrupt(const struct timespec* timeout_ns = nullptr);

    /**
     * @brief 获取中断 line（用于异步 IO）
     */
    struct gpiod_line* get_irq_line() const { return irq_line_; }

    /**
     * @brief 控制 CM4 GPIO24（悬崖传感器使能）
     * @param enable true=拉高，false=拉低
     * @return 成功返回 true
     */
    bool set_irs_en(bool enable);

    /**
     * @brief 检查是否已初始化
     */
    bool is_initialized() const { return initialized_; }

    /**
     * @brief 获取输出缓存（16-bit），用于测试与调试
     */
    uint16_t get_output_cache() const { return output_cache_; }

    /**
     * @brief 清理资源
     */
    void cleanup();

    bool enable_cliff(bool enable);

private:
    bool initialized_ = false;

    // PCA9535 GPIO chip (gpiochip2)
    struct gpiod_chip* chip_ = nullptr;
    struct gpiod_line_bulk lines_;  ///< 16 个 GPIO 线
    std::array<struct gpiod_line*, 16> line_array_{};

    // 中断 GPIO (CM4 GPIO1)
    struct gpiod_chip* irq_chip_ = nullptr;
    struct gpiod_line* irq_line_ = nullptr;

    // 悬崖传感器使能 GPIO (CM4 GPIO24)
    struct gpiod_line* irs_en_line_ = nullptr;

    // 输出引脚缓存
    uint16_t output_cache_ = 0x0000;
};

} // namespace extio
} // namespace doly
