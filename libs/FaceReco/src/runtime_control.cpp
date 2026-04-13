#include "doly/vision/runtime_control.hpp"

namespace doly::vision {

RuntimeControl::RuntimeControl() = default;

void RuntimeControl::setEnabled(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (enabled_ == enabled) {
            // 仍然可能需要唤醒等待线程（例如 capture 队列仍有待处理请求）
            enabled_ = enabled;
        } else {
            enabled_ = enabled;
        }
    }
    cv_.notify_all();
}

bool RuntimeControl::isEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return enabled_ && !shutdown_;
}

void RuntimeControl::setStreamingEnabled(bool enabled) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        streaming_enabled_ = enabled;
    }
    cv_.notify_all();
}

bool RuntimeControl::isStreamingEnabled() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return streaming_enabled_ && !shutdown_;
}

void RuntimeControl::requestShutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shutdown_ = true;
    }
    cv_.notify_all();
}

bool RuntimeControl::isShutdownRequested() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return shutdown_;
}

bool RuntimeControl::waitUntilEnabled() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&]() { return shutdown_ || enabled_; });
    return !shutdown_ && enabled_;
}

bool RuntimeControl::waitUntilEnabledOrCaptureRequested() {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [&]() {
        return shutdown_ || enabled_ || !capture_queue_.empty();
    });
    return !shutdown_ && (enabled_ || !capture_queue_.empty());
}

void RuntimeControl::enqueueCaptureRequest(const CaptureRequest& request) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        capture_queue_.push(request);
    }
    cv_.notify_all();
}

bool RuntimeControl::tryPopCaptureRequest(CaptureRequest& out_request) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (capture_queue_.empty()) {
        return false;
    }
    out_request = capture_queue_.front();
    capture_queue_.pop();
    return true;
}

bool RuntimeControl::hasPendingCaptureRequest() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return !capture_queue_.empty();
}

}  // namespace doly::vision
