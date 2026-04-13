#pragma once

#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <string>
#include <atomic>
#include <nlohmann/json.hpp>

namespace doly::vision {

struct CaptureRequest {
    std::string request_id;
    nlohmann::json params;
};

/**
 * @brief 运行时控制器
 *
 * 负责在检测线程与消息总线命令线程之间传递控制信号：
 *  - 启用/禁用检测
 *  - 控制视频流输出
 *  - 快照/识别请求
 */
class RuntimeControl {
public:
    RuntimeControl();

    void setEnabled(bool enabled);
    [[nodiscard]] bool isEnabled() const;

    void setStreamingEnabled(bool enabled);
    [[nodiscard]] bool isStreamingEnabled() const;

    void requestShutdown();
    [[nodiscard]] bool isShutdownRequested() const;

    /**
     * @brief 阻塞等待，直到检测被启用或收到关闭请求。
     * @return true 如果已启用；false 表示检测应终止
     */
    bool waitUntilEnabled();

    /**
     * @brief 阻塞等待，直到检测被启用、收到抓拍请求或收到关闭请求。
     * @return true 如果已有可处理工作；false 表示检测应终止
     */
    bool waitUntilEnabledOrCaptureRequested();

    /**
     * @brief 推入一次性的拍照/识别请求
     */
    void enqueueCaptureRequest(const CaptureRequest& request);

    /**
     * @brief 尝试弹出一个待处理的拍照请求
     * @param out_request 输出参数
     * @return 是否成功弹出
     */
    bool tryPopCaptureRequest(CaptureRequest& out_request);

    /**
     * @brief 是否存在待处理的抓拍/录像请求
     */
    [[nodiscard]] bool hasPendingCaptureRequest() const;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;

    bool enabled_{true};
    bool streaming_enabled_{true};
    bool shutdown_{false};

    std::queue<CaptureRequest> capture_queue_;
};

}  // namespace doly::vision
