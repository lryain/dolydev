/**
 * @file pca9535_hal.cpp
 * @brief PCA9535 HAL 层实现
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/pca9535_hal.hpp"
#include <iostream>
#include <cstring>

namespace doly {
namespace extio {

Pca9535Hal::~Pca9535Hal() {
    cleanup();
}

bool Pca9535Hal::init(const char* chip_name,
                      const char* irq_chip_name,
                      unsigned int irq_line_num) {
    if (initialized_) {
        std::cerr << "[PCA9535 HAL] Already initialized" << std::endl;
        return false;
    }

    // 打开 PCA9535 gpiochip (gpiochip2)
    chip_ = gpiod_chip_open_by_name(chip_name);
    if (!chip_) {
        std::cerr << "[PCA9535 HAL] Failed to open " << chip_name << std::endl;
        return false;
    }

    // 获取 16 个 GPIO 线
    for (unsigned int i = 0; i < 16; i++) {
        line_array_[i] = gpiod_chip_get_line(chip_, i);
        if (!line_array_[i]) {
            std::cerr << "[PCA9535 HAL] Failed to get line " << i << std::endl;
            cleanup();
            return false;
        }
    }

    // 初始化 bulk 结构
    gpiod_line_bulk_init(&lines_);
    for (auto* line : line_array_) {
        gpiod_line_bulk_add(&lines_, line);
    }

    // 请求所有线为输入（逐个请求）
    for (auto* line : line_array_) {
        if (gpiod_line_request_input(line, "pca9535_hal") < 0) {
            std::cerr << "[PCA9535 HAL] Failed to request input" << std::endl;
            cleanup();
            return false;
        }
    }

    // 打开中断 GPIO chip
    irq_chip_ = gpiod_chip_open_by_name(irq_chip_name);
    if (!irq_chip_) {
        std::cerr << "[PCA9535 HAL] Failed to open IRQ chip " << irq_chip_name << std::endl;
        cleanup();
        return false;
    }

    // 获取中断 GPIO 线
    irq_line_ = gpiod_chip_get_line(irq_chip_, irq_line_num);
    if (!irq_line_) {
        std::cerr << "[PCA9535 HAL] Failed to get IRQ line " << irq_line_num << std::endl;
        cleanup();
        return false;
    }

    // 请求中断：下降沿触发
    if (gpiod_line_request_falling_edge_events(irq_line_, "pca9535_irq") < 0) {
        std::cerr << "[PCA9535 HAL] Failed to request IRQ events" << std::endl;
        cleanup();
        return false;
    }
    
    // 2. IRS_EN (CM4 GPIO24) GPIO24不再作为IR使能，而是改为分默认使能，GPIO24配给编码器
    // // 获取悬崖传感器使能 GPIO 线 (CM4 GPIO24)
    // irs_en_line_ = gpiod_chip_get_line(irq_chip_, irs_en_line_num);
    // if (!irs_en_line_) {
    //     std::cerr << "[PCA9535 HAL] Failed to get IRS_EN line " << irs_en_line_num << std::endl;
    //     cleanup();
    //     return false;
    // }

    // // 请求 GPIO24 为输出，默认拉低
    // if (gpiod_line_request_output(irs_en_line_, "irs_en", 0) < 0) {
    //     std::cerr << "[PCA9535 HAL] Failed to request IRS_EN output" << std::endl;
    //     cleanup();
    //     return false;
    // }

    initialized_ = true;
    std::cout << "[PCA9535 HAL] Initialized: chip=" << chip_name 
              << ", irq=" << irq_chip_name << ":" << irq_line_num 
              << std::endl;
    return true;
}

uint16_t Pca9535Hal::bulk_read() {
    if (!initialized_) {
        std::cerr << "[PCA9535 HAL] Not initialized" << std::endl;
        return 0;
    }

    // 批量读取 16 个 GPIO 值（逐个读取）
    uint16_t state = 0;
    for (int i = 0; i < 16; i++) {
        int value = gpiod_line_get_value(line_array_[i]);
        if (value > 0) {
            state |= (1u << i);
        }
    }
    // 把state转换成二进制打印出来
    // for (int i = 15; i >= 0; i--) {
    //     std::cout << ((state >> i) & 1);
    // }
    // std::cout << std::endl;

    return state;
}

bool Pca9535Hal::read_pin(Pca9535Pin pin) {
    if (!initialized_) {
        return false;
    }

    uint8_t index = static_cast<uint8_t>(pin);
    if (index >= 16) {
        return false;
    }

    int value = gpiod_line_get_value(line_array_[index]);
    return value > 0;
}

bool Pca9535Hal::write_pin(Pca9535Pin pin, bool value) {
    if (!initialized_) {
        return false;
    }

    uint8_t index = static_cast<uint8_t>(pin);
    if (index >= 16) {
        return false;
    }

    // 释放当前请求，改为输出模式
    gpiod_line_release(line_array_[index]);
    
    if (gpiod_line_request_output(line_array_[index], "pca9535_out", value ? 1 : 0) < 0) {
        std::cerr << "[PCA9535 HAL] Failed to set output on pin " << (int)index << std::endl;
        return false;
    }

    // 更新缓存
    if (value) {
        output_cache_ |= (1u << index);
    } else {
        output_cache_ &= ~(1u << index);
    }

    return true;
}

bool Pca9535Hal::write_bulk(uint16_t state, uint16_t mask) {
    if (!initialized_) {
        return false;
    }

    bool success = true;
    for (int i = 0; i < 16; i++) {
        if (mask & (1u << i)) {
            bool value = state & (1u << i);
            if (!write_pin(static_cast<Pca9535Pin>(i), value)) {
                success = false;
            }
        }
    }
    return success;
}

bool Pca9535Hal::wait_interrupt(const struct timespec* timeout_ns) {
    if (!initialized_ || !irq_line_) {
        return false;
    }

    // 等待中断事件
    int ret = gpiod_line_event_wait(irq_line_, timeout_ns);
    if (ret <= 0) {
        return false;  // 超时或错误
    }

    // 读取并清除事件
    struct gpiod_line_event event;
    if (gpiod_line_event_read(irq_line_, &event) < 0) {
        return false;
    }

    return true;
}

// bool Pca9535Hal::set_irs_en(bool enable) {
//     if (!initialized_ || !irs_en_line_) {
//         return false;
//     }
//     printf("------> [PCA9535 HAL] set_irs_en to %d\n", enable ? 1 : 0);
//     // 设置 GPIO24 电平
//     if (gpiod_line_set_value(irs_en_line_, enable ? 1 : 0) < 0) {
//         std::cerr << "[PCA9535 HAL] Failed to set IRS_EN" << std::endl;
//         return false;
//     }

//     return true;
// }

bool Pca9535Hal::enable_cliff(bool enable) {
    // 悬崖传感器需要同时控制两个引脚：
    // 1. IRS_DRV (PCA9535 pin 15)
    // 2. IRS_EN (CM4 GPIO24) GPIO24不再作为IR使能，而是改为分默认使能，GPIO24配给编码器
    // bool irs_drv_ok = set_output(Pca9535Pin::IRS_DRV, enable);
    bool irs_drv_ok = write_pin(Pca9535Pin::IRS_DRV, enable);
    // bool irs_en_ok = hal_.set_irs_en(enable);
    return irs_drv_ok;
}

void Pca9535Hal::cleanup() {
    // 由于Cliff的IR使能使用了 cm4 的gpio 需要手动在这里释放
    printf("[PCA9535 HAL] cleanup -> enable_cliff...\n");
    // set_irs_en(false);
    enable_cliff(false);
    // if(irs_en_line_) {
    //     printf("----------> [PCA9535 HAL] Releasing IRS_EN line\n");
    //     set_irs_en(false);
    //     gpiod_line_release(irs_en_line_);
    //     irs_en_line_ = nullptr;
    // }
    // if (drv_line_) {
    //     gpiod_line_set_value(drv_line_, 0);
    // }
    // if (irs_en_line_) {
    //     gpiod_line_set_value(irs_en_line_, 0);
    // }
    if (irq_line_) {
        gpiod_line_release(irq_line_);
        irq_line_ = nullptr;
    }

    if (irq_chip_) {
        gpiod_chip_close(irq_chip_);
        irq_chip_ = nullptr;
    }

    // 释放所有 GPIO 线
    for (auto* line : line_array_) {
        if (line) {
            gpiod_line_release(line);
        }
    }

    if (chip_) {
        gpiod_chip_close(chip_);
        chip_ = nullptr;
    }

    initialized_ = false;
}

} // namespace extio
} // namespace doly
