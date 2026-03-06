/**
 * @file video_event_bridge.cpp
 * @brief 视频事件桥接实现
 */

#include "nora/coms/msgbus_bridge/video_event_bridge.h"

#include <thread>
#include <chrono>
#include <iostream>
#include <sstream>

namespace nora::coms::msgbus_bridge {

// ============================================================================
// VideoEventBridge Implementation
// ============================================================================

VideoEventBridge::VideoEventBridge(
    nora::coms::video::VideoIpcManager& manager,
    const std::string& stream_name)
    : manager_(manager),
      stream_name_(stream_name),
      is_running_(false),
      poll_interval_ms_(100) {
  // 默认禁用所有事件
  for (int i = 0; i < 6; ++i) {
    event_enabled_[i] = false;
  }
}

VideoEventBridge::~VideoEventBridge() {
  if (is_running_) {
    Stop();
  }
}

void VideoEventBridge::EnableEvent(VideoEventType event_type, bool enabled) {
  int idx = static_cast<int>(event_type);
  if (idx >= 0 && idx < 6) {
    event_enabled_[idx] = enabled;
  }
}

bool VideoEventBridge::IsEventEnabled(VideoEventType event_type) const {
  int idx = static_cast<int>(event_type);
  if (idx >= 0 && idx < 6) {
    return event_enabled_[idx];
  }
  return false;
}

bool VideoEventBridge::Start(unsigned int poll_interval_ms) {
  if (is_running_) {
    return false;  // 已经在运行
  }

  poll_interval_ms_ = poll_interval_ms;
  is_running_ = true;

  // 启动后台监听线程
  // 注意：这里使用线程来实现事件监听
  // 完整实现需要存储线程并支持 Stop() 操作
  std::thread(&VideoEventBridge::EventMonitorThread, this).detach();

  return true;
}

void VideoEventBridge::Stop() {
  is_running_ = false;
  // 实际实现中需要等待线程结束
  std::this_thread::sleep_for(
      std::chrono::milliseconds(poll_interval_ms_ + 50));
}

bool VideoEventBridge::IsRunning() const {
  return is_running_;
}

bool VideoEventBridge::PublishEvent(const VideoEventPayload& payload) {
  // 这里应该调用 msgbus 发布事件
  // 格式：Topic: "video.{stream_name}.{event_type}"
  // Payload: JSON 格式
  std::string topic = GetTopicName(payload.event_type);
  std::string json_payload = PayloadToJson(payload);

  std::cout << "[VideoEventBridge] Publish event to topic: " << topic
            << "\n  Payload: " << json_payload << std::endl;

  // TODO: 实际调用 msgbus 发布接口
  // msgbus::Publisher pub("nora.video");
  // pub.publish(topic, json_payload);

  return true;
}

uint64_t VideoEventBridge::GetLastEventTimestamp() const {
  auto stats = manager_.GetStatistics();
  return stats.last_update_ns;
}

void VideoEventBridge::EventMonitorThread() {
  uint64_t last_frame_id = 0;

  while (is_running_) {
    std::this_thread::sleep_for(
        std::chrono::milliseconds(poll_interval_ms_));

    if (!manager_.IsInitialized()) {
      continue;
    }

    // 获取最新统计
    auto stats = manager_.GetStatistics();

    // 检查是否有新帧
    if (IsEventEnabled(VideoEventType::kFrameAvailable) &&
        stats.last_frame_id > last_frame_id) {
      VideoEventPayload payload{
          VideoEventType::kFrameAvailable,
          stats.last_frame_id,
          stats.last_update_ns,
          0,
          ""};

      PublishEvent(payload);
      last_frame_id = stats.last_frame_id;
    }

    // 可以在这里添加其他事件检查逻辑
  }
}

std::string VideoEventBridge::GetTopicName(VideoEventType event_type) const {
  std::ostringstream oss;
  oss << "video." << stream_name_ << ".";

  switch (event_type) {
    case VideoEventType::kFrameAvailable:
      oss << "frame_available";
      break;
    case VideoEventType::kFrameDropped:
      oss << "frame_dropped";
      break;
    case VideoEventType::kStreamStarted:
      oss << "stream_started";
      break;
    case VideoEventType::kStreamStopped:
      oss << "stream_stopped";
      break;
    case VideoEventType::kStreamError:
      oss << "stream_error";
      break;
    default:
      oss << "unknown";
  }

  return oss.str();
}

std::string VideoEventBridge::PayloadToJson(
    const VideoEventPayload& payload) const {
  std::ostringstream oss;
  oss << "{"
      << "\"event_type\": " << static_cast<int>(payload.event_type)
      << ", \"frame_id\": " << payload.frame_id
      << ", \"timestamp_ns\": " << payload.timestamp_ns
      << ", \"error_code\": " << payload.error_code;

  if (!payload.metadata.empty()) {
    oss << ", \"metadata\": \"" << payload.metadata << "\"";
  }

  oss << "}";
  return oss.str();
}

}  // namespace nora::coms::msgbus_bridge
