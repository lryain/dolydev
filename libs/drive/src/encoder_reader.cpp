/*

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "drive/encoder_reader.hpp"
#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <iostream>
#include <utility>

static const char* DEFAULT_CHIP = "gpiochip0"; // 固定默认 gpio 芯片

EncoderReader::EncoderReader(int pin_a, int pin_b, const std::string& name)
    : chip(nullptr), line_a(nullptr), line_b(nullptr),
      position(0), last_snapshot(0), enabled(false),
      gpio_a(pin_a), gpio_b(pin_b),
      channel_name(name.empty() ? "encoder" : name),
      is_left_channel(true), user_cb(nullptr) {
    std::string lower = channel_name;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (lower.find("right") != std::string::npos) {
        is_left_channel = false;
    }
}

EncoderReader::~EncoderReader() {
    stop();
}

bool EncoderReader::init() {
    if (chip) {
        return true;
    }

    chip_name = std::string(DEFAULT_CHIP);
    chip = gpiod_chip_open_by_name(chip_name.c_str());
    if (!chip) {
        std::cerr << "[EncoderReader] failed open gpio chip '" << chip_name << "': " << strerror(errno) << std::endl;
        return false;
    }

    line_a = gpiod_chip_get_line(chip, gpio_a);
    line_b = gpiod_chip_get_line(chip, gpio_b);

    if (!line_a || !line_b) {
        std::cerr << "[EncoderReader] failed to get encoder lines (chip=" << chip_name
                  << ", a=" << gpio_a << ", b=" << gpio_b << ")" << std::endl;
        cleanupLines();
        return false;
    }

    std::string request_label = channel_name.empty() ? "encoder_reader" : channel_name;

    if (gpiod_line_request_input(line_b, request_label.c_str()) < 0) {
        std::cerr << "[EncoderReader] failed request input on gpio=" << gpio_b << ": "
                  << strerror(errno) << std::endl;
        cleanupLines();
        return false;
    }

    if (gpiod_line_request_both_edges_events(line_a, request_label.c_str()) < 0) {
        std::cerr << "[EncoderReader] failed request events on gpio=" << gpio_a << ": "
                  << strerror(errno) << std::endl;
        cleanupLines();
        return false;
    }

    position.store(0);
    last_snapshot.store(0);

    return true;
}

void EncoderReader::start() {
    if (!init()) {
        return;
    }

    bool already_running = enabled.exchange(true);
    if (already_running) {
        return;
    }

    worker_thread = std::thread(&EncoderReader::workerLoop, this);
}

void EncoderReader::stop() {
    bool was_running = enabled.exchange(false);
    if (worker_thread.joinable()) {
        worker_thread.join();
    }

    cleanupLines();

    if (was_running) {
        position.store(0);
        last_snapshot.store(0);
    }
}

void EncoderReader::setCallback(Callback cb) {
    user_cb = std::move(cb);
}

long EncoderReader::getPosition() const {
    return static_cast<long>(position.load());
}

long EncoderReader::getDeltaPosition() {
    int64_t current = position.load();
    int64_t previous = last_snapshot.exchange(current);
    return static_cast<long>(current - previous);
}

double EncoderReader::getVelocity() {
    // 简易估算：假定调用周期约为 10ms，返回大致脉冲/秒。
    long delta = getDeltaPosition();
    return static_cast<double>(delta) / 0.01; // ticks per second (approx)
}

void EncoderReader::setDebugEnabled(bool enabled_flag) {
    (void)enabled_flag;
}

void EncoderReader::workerLoop() {
    struct gpiod_line_event ev;
    struct timespec ts { 0, 200000000 }; // 200ms

    while (enabled.load()) {
        int rc = gpiod_line_event_wait(line_a, &ts);
        if (rc < 0) {
            if (errno != EINTR) {
                std::cerr << "[EncoderReader] event_wait error on gpio=" << gpio_a
                          << ": " << strerror(errno) << std::endl;
            }
            continue;
        }
        if (rc == 0) {
            continue; // timeout
        }
        if (gpiod_line_event_read(line_a, &ev) != 0) {
            continue;
        }

        int b = gpiod_line_get_value(line_b);
        if (b < 0) {
            continue;
        }

        if (ev.event_type == GPIOD_LINE_EVENT_RISING_EDGE) {
            if (b == 0) {
                position.fetch_add(1);
            } else {
                position.fetch_sub(1);
            }

            if (user_cb) {
                if (is_left_channel) {
                    user_cb(position.load(), 0);
                } else {
                    user_cb(0, position.load());
                }
            }
        }
    }
}

void EncoderReader::cleanupLines() {
    if (line_a) {
        gpiod_line_release(line_a);
        line_a = nullptr;
    }
    if (line_b) {
        gpiod_line_release(line_b);
        line_b = nullptr;
    }
    if (chip) {
        gpiod_chip_close(chip);
        chip = nullptr;
    }
}
