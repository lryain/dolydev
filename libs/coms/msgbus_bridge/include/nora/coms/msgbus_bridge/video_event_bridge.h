/**
 * @file video_event_bridge.h
 * @brief 视频 IPC 与 msgbus 事件系统的桥接
 * 
 * 将视频 IPC 的关键事件发布到 msgbus，实现系统级的松耦合：
 * - VIDEO_FRAME_AVAILABLE: 新帧可用
 * - VIDEO_FRAME_DROPPED: 帧被丢弃
 * - VIDEO_STREAM_ERROR: IPC 错误
 * 
 * @author Nora Project
 * @version 1.0.0
 */

#pragma once

#include "nora/coms/video/video_ipc_manager.h"

#include <memory>
#include <string>
#include <cstdint>
#include <functional>

namespace nora::coms::msgbus_bridge {

/**
 * @brief 视频事件类型定义
 */
enum class VideoEventType {
  kFrameAvailable = 1,    ///< 新帧可用
  kFrameDropped = 2,      ///< 帧被丢弃
  kStreamStarted = 3,     ///< 视频流开始
  kStreamStopped = 4,     ///< 视频流停止
  kStreamError = 5,       ///< 流错误
};

/**
 * @struct VideoEventPayload
 * @brief 视频事件载荷
 */
struct VideoEventPayload {
  /// 事件类型
  VideoEventType event_type;

  /// 帧 ID
  uint64_t frame_id;

  /// 时间戳（纳秒）
  uint64_t timestamp_ns;

  /// 错误码（仅在错误事件中有效）
  int error_code;

  /// 额外数据（JSON 或其他格式）
  std::string metadata;
};

/**
 * @class VideoEventBridge
 * @brief 视频事件桥接管理器
 * 
 * 监听视频 IPC 的状态变化，将重要事件发布到 msgbus。
 * 
 * 使用示例：
 *   VideoIpcManager manager("video_stream", 0);
 *   VideoEventBridge bridge(manager, "eyeengine");
 *   
 *   bridge.EnableEvent(VideoEventType::kFrameAvailable, true);
 *   bridge.Start();  // 后台监听线程
 *   
 *   // 在其他地方监听 msgbus 事件
 *   // msgbus 会发布：
 *   //   Topic: "video.eyeengine.frame_available"
 *   //   Payload: JSON 格式的 VideoEventPayload
 */
class VideoEventBridge {
 public:
  /**
   * @brief 构造函数
   * 
   * @param manager 视频 IPC 管理器引用
   * @param stream_name 流名称（用于生成 msgbus topic）
   */
  VideoEventBridge(nora::coms::video::VideoIpcManager& manager,
                   const std::string& stream_name = "default");

  /**
   * @brief 析构函数
   */
  ~VideoEventBridge();

  // 禁用拷贝
  VideoEventBridge(const VideoEventBridge&) = delete;
  VideoEventBridge& operator=(const VideoEventBridge&) = delete;

  /**
   * @brief 启用或禁用特定事件
   * 
   * @param event_type 事件类型
   * @param enabled 是否启用
   */
  void EnableEvent(VideoEventType event_type, bool enabled);

  /**
   * @brief 检查事件是否启用
   */
  bool IsEventEnabled(VideoEventType event_type) const;

  /**
   * @brief 启动后台事件监听线程
   * 
   * @param poll_interval_ms 轮询间隔（毫秒），默认 100ms
   * @return true 成功启动，false 已在运行或初始化失败
   */
  bool Start(unsigned int poll_interval_ms = 100);

  /**
   * @brief 停止后台事件监听线程
   */
  void Stop();

  /**
   * @brief 检查是否正在运行
   */
  bool IsRunning() const;

  /**
   * @brief 获取流名称
   */
  const std::string& GetStreamName() const { return stream_name_; }

  /**
   * @brief 手动发布事件（高级用法）
   * 
   * @param payload 事件载荷
   * @return true 发布成功
   */
  bool PublishEvent(const VideoEventPayload& payload);

  /**
   * @brief 获取最后一个事件的时间戳
   */
  uint64_t GetLastEventTimestamp() const;

 private:
  /**
   * @brief 后台事件监听线程
   */
  void EventMonitorThread();

  /**
   * @brief 将事件类型转换为 msgbus topic
   */
  std::string GetTopicName(VideoEventType event_type) const;

  /**
   * @brief 将事件载荷转换为 JSON
   */
  std::string PayloadToJson(const VideoEventPayload& payload) const;

  nora::coms::video::VideoIpcManager& manager_;
  std::string stream_name_;

  /// 事件启用标志
  bool event_enabled_[6];

  bool is_running_;
  unsigned int poll_interval_ms_;

  // 后台线程会在实现中创建
};

}  // namespace nora::coms::msgbus_bridge
