#pragma once

#include <atomic>
#include <string>
#include <thread>
#include <functional>

#include <gpiod.h>

// 基于 libgpiod 的单通道编码器读取器。每个实例仅负责一组 A/B 引脚。
class EncoderReader {
public:
    using Callback = std::function<void(int64_t left, int64_t right)>;

    EncoderReader(int pin_a = 6, int pin_b = 24, const std::string& name = "encoder");
    ~EncoderReader();

    bool init();
    void start();
    void stop();
    void setCallback(Callback cb);

    long getPosition() const;
    long getDeltaPosition();
    double getVelocity();

    void setDebugEnabled(bool enabled);

private:
    void workerLoop();
    void cleanupLines();

    struct gpiod_chip* chip;
    std::string chip_name;
    struct gpiod_line* line_a;
    struct gpiod_line* line_b;

    std::thread worker_thread;

    std::atomic<int64_t> position;
    std::atomic<int64_t> last_snapshot;
    std::atomic<bool> enabled;

    int gpio_a;
    int gpio_b;

    std::string channel_name;
    bool is_left_channel;

    Callback user_cb;
};
