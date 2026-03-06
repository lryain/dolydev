#pragma once

#include <string>
#include <vector>
#include <cstdint>

namespace msgbus {

enum class MessageType : uint16_t {
    UNKNOWN = 0,
    VAD_SPEECH_SEGMENT = 5,
    VAD_NOISE_SEGMENT = 6,
    CONTROL_PAUSE = 20,
    CONTROL_RESUME = 21,
    FACE_DETECTED = 31,      // {track_id}|{x1}|{y1}|{x2}|{y2}|{confidence} - 检测到人脸
    FACE_RECOGNITION = 30,   // {person_name}|{track_id}|{timestamp}|{confidence} - 识别到人脸
    FACE_DISAPPEARED = 32,   // {track_id}|{lost_frames} - 人脸消失
    // 未来还要添加检测到假人脸事件
};

struct Message {
    MessageType type{MessageType::UNKNOWN};
    std::string source;
    std::string data; // raw payload (changed from vector<uint8_t> to string for simplicity)

    // simple string serialization for now: type|source|data
    std::string serialize() const;
    static Message deserialize(const std::string &s);
};

} // namespace msgbus
