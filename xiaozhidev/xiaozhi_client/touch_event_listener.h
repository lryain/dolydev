#pragma once

#include <functional>
#include <atomic>
#include <thread>

class TouchEventListener {
public:
    using TouchCallback = std::function<void()>;

    TouchEventListener();
    ~TouchEventListener();

    bool start(TouchCallback single_tap, TouchCallback double_tap);
    void stop();

private:
    void run();

    std::atomic<bool> running_;
    std::thread worker_;
    TouchCallback single_tap_cb_;
    TouchCallback double_tap_cb_;
};
