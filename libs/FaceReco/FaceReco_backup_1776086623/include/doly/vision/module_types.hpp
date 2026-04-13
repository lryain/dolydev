#pragma once

/**
 * @file module_types.hpp
 * @brief Vision Service 模块类型定义
 * 
 * 定义视觉服务的运行模式、模块状态和模块类型。
 */

#include <string>
#include <vector>
#include <map>

namespace doly::vision {

/**
 * @brief 视觉服务运行模式
 */
enum class RunMode {
    IDLE,           ///< 空载模式（不加载任何功能模块）
    STREAM_ONLY,    ///< 仅视频推流（摄像头采集 + 推流）
    DETECT_ONLY,    ///< 仅人脸检测（不识别）
    DETECT_TRACK,   ///< 人脸检测 + 跟踪（不识别）
    FULL,           ///< 完整模式（检测 + 识别 + 推流）
    CUSTOM          ///< 自定义模块组合
};

/**
 * @brief 模块状态
 */
enum class ModuleState {
    UNLOADED,   ///< 未加载
    LOADING,    ///< 加载中
    RUNNING,    ///< 运行中
    PAUSED,     ///< 已暂停
    ERROR       ///< 错误状态
};

/**
 * @brief 可加载的功能模块类型
 */
enum class ModuleType {
    CAMERA_CAPTURE,     ///< 摄像头采集
    FACE_DETECTION,     ///< 人脸检测
    FACE_RECOGNITION,   ///< 人脸识别
    FACE_TRACKING,      ///< 人脸跟踪
    LIVENESS_DETECTION, ///< 活体检测
    VIDEO_STREAM,       ///< 视频流发布
    PHOTO_CAPTURE,      ///< 拍照
    VIDEO_RECORD        ///< 录像
};

/**
 * @brief 将 RunMode 转换为字符串
 */
inline std::string runModeToString(RunMode mode) {
    switch (mode) {
        case RunMode::IDLE:         return "IDLE";
        case RunMode::STREAM_ONLY:  return "STREAM_ONLY";
        case RunMode::DETECT_ONLY:  return "DETECT_ONLY";
        case RunMode::DETECT_TRACK: return "DETECT_TRACK";
        case RunMode::FULL:         return "FULL";
        case RunMode::CUSTOM:       return "CUSTOM";
        default:                    return "UNKNOWN";
    }
}

/**
 * @brief 从字符串解析 RunMode
 */
inline RunMode stringToRunMode(const std::string& str) {
    if (str == "IDLE")          return RunMode::IDLE;
    if (str == "STREAM_ONLY")   return RunMode::STREAM_ONLY;
    if (str == "DETECT_ONLY")   return RunMode::DETECT_ONLY;
    if (str == "DETECT_TRACK")  return RunMode::DETECT_TRACK;
    if (str == "FULL")          return RunMode::FULL;
    if (str == "CUSTOM")        return RunMode::CUSTOM;
    return RunMode::IDLE;  // 默认
}

/**
 * @brief 将 ModuleState 转换为字符串
 */
inline std::string moduleStateToString(ModuleState state) {
    switch (state) {
        case ModuleState::UNLOADED: return "unloaded";
        case ModuleState::LOADING:  return "loading";
        case ModuleState::RUNNING:  return "running";
        case ModuleState::PAUSED:   return "paused";
        case ModuleState::ERROR:    return "error";
        default:                    return "unknown";
    }
}

/**
 * @brief 将 ModuleType 转换为字符串
 */
inline std::string moduleTypeToString(ModuleType type) {
    switch (type) {
        case ModuleType::CAMERA_CAPTURE:     return "CAMERA_CAPTURE";
        case ModuleType::FACE_DETECTION:     return "FACE_DETECTION";
        case ModuleType::FACE_RECOGNITION:   return "FACE_RECOGNITION";
        case ModuleType::FACE_TRACKING:      return "FACE_TRACKING";
        case ModuleType::LIVENESS_DETECTION: return "LIVENESS_DETECTION";
        case ModuleType::VIDEO_STREAM:       return "VIDEO_STREAM";
        case ModuleType::PHOTO_CAPTURE:      return "PHOTO_CAPTURE";
        case ModuleType::VIDEO_RECORD:       return "VIDEO_RECORD";
        default:                             return "UNKNOWN";
    }
}

/**
 * @brief 从字符串解析 ModuleType
 */
inline ModuleType stringToModuleType(const std::string& str) {
    if (str == "CAMERA_CAPTURE")     return ModuleType::CAMERA_CAPTURE;
    if (str == "FACE_DETECTION")     return ModuleType::FACE_DETECTION;
    if (str == "FACE_RECOGNITION")   return ModuleType::FACE_RECOGNITION;
    if (str == "FACE_TRACKING")      return ModuleType::FACE_TRACKING;
    if (str == "LIVENESS_DETECTION") return ModuleType::LIVENESS_DETECTION;
    if (str == "VIDEO_STREAM")       return ModuleType::VIDEO_STREAM;
    if (str == "PHOTO_CAPTURE")      return ModuleType::PHOTO_CAPTURE;
    if (str == "VIDEO_RECORD")       return ModuleType::VIDEO_RECORD;
    return ModuleType::CAMERA_CAPTURE;  // 默认
}

/**
 * @brief 获取指定运行模式需要的模块列表
 */
inline std::vector<ModuleType> getModulesForMode(RunMode mode) {
    switch (mode) {
        case RunMode::IDLE:
            return {};
            
        case RunMode::STREAM_ONLY:
            return {ModuleType::CAMERA_CAPTURE, ModuleType::VIDEO_STREAM};
            
        case RunMode::DETECT_ONLY:
            return {ModuleType::CAMERA_CAPTURE, ModuleType::FACE_DETECTION};
            
        case RunMode::DETECT_TRACK:
            return {ModuleType::CAMERA_CAPTURE, ModuleType::FACE_DETECTION, 
                    ModuleType::FACE_TRACKING};
            
        case RunMode::FULL:
            return {ModuleType::CAMERA_CAPTURE, ModuleType::FACE_DETECTION,
                    ModuleType::FACE_TRACKING, ModuleType::FACE_RECOGNITION,
                    ModuleType::LIVENESS_DETECTION, ModuleType::VIDEO_STREAM};
            
        case RunMode::CUSTOM:
        default:
            return {};
    }
}

/**
 * @brief 模块信息结构体
 */
struct ModuleInfo {
    ModuleType type;
    ModuleState state;
    std::string error_message;
    int64_t load_time_ms;
    int64_t uptime_ms;
};

/**
 * @brief 视觉服务状态
 */
struct VisionServiceState {
    bool enabled;
    RunMode mode;
    bool streaming;
    double fps;
    int active_tracks;
    int recognized_count;
    int64_t uptime_seconds;
    std::map<ModuleType, ModuleState> module_states;
};

}  // namespace doly::vision
