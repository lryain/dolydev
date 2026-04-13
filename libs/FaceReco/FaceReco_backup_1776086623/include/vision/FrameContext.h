#pragma once

#include <opencv2/core.hpp>
#include <map>
#include <any>
#include <memory>
#include <cstdint>
#include <string>

/**
 * @struct FrameMetadata
 * @brief 帧级元数据，记录帧的基本属性和检测结果
 */
struct FrameMetadata {
    int frame_id = 0;                    // 帧编号
    int64_t timestamp_us = 0;            // 时间戳（微秒）
    float brightness = 0.0f;             // 平均亮度 [0, 1]
    float motion_level = 0.0f;           // 运动强度 [0, 1]
    std::string source = "unknown";      // 输入源，如 "camera_0", "video_file"
    int processing_time_ms = 0;          // 总处理时间（毫秒）
};

/**
 * @class FrameContext
 * @brief 帧级数据容器，在管道中传递
 * 
 * 包含：
 * - 原始图像数据和处理后的显示图像
 * - 帧级元数据
 * - 模块间通信的数据总线（data_bus）
 */
class FrameContext {
public:
    // ==================== 图像数据 ====================
    
    cv::Mat raw_frame;                   // 原始帧数据
    cv::Mat display_frame;               // 用于绘制调试信息的帧副本

    // ==================== 元数据 ====================
    
    FrameMetadata metadata;

    // ==================== 模块间通信 ====================
    
    /**
     * 通用数据存储，模块通过 key-value 对进行通信
     * 常用 key：
     * - "motion_mask"        : cv::Mat
     * - "motion_detected"    : bool
     * - "face_detections"    : std::vector<cv::Rect>
     * - "recognized_names"   : std::vector<std::string>
     */
    std::map<std::string, std::any> data_bus;

    // ==================== 便捷方法 ====================

    /**
     * @brief 设置数据到 data_bus
     * @tparam T 数据类型
     * @param key 键
     * @param value 值
     */
    template<typename T>
    void SetData(const std::string& key, const T& value) {
        data_bus[key] = value;
    }

    /**
     * @brief 从 data_bus 读取数据
     * @tparam T 数据类型
     * @param key 键
     * @param default_val 如果键不存在，返回的默认值
     * @return 数据值
     */
    template<typename T>
    T GetData(const std::string& key, const T& default_val = T()) const {
        auto it = data_bus.find(key);
        if (it != data_bus.end()) {
            try {
                return std::any_cast<T>(it->second);
            } catch (const std::bad_any_cast&) {
                return default_val;
            }
        }
        return default_val;
    }

    /**
     * @brief 检查 data_bus 中是否存在某个键
     * @param key 键
     * @return 是否存在
     */
    bool HasData(const std::string& key) const {
        return data_bus.find(key) != data_bus.end();
    }

    /**
     * @brief 删除 data_bus 中的某个键
     * @param key 键
     */
    void ClearData(const std::string& key) {
        data_bus.erase(key);
    }

    /**
     * @brief 清空所有 data_bus 数据
     */
    void ClearAllData() {
        data_bus.clear();
    }

    /**
     * @brief 验证帧是否有效（图像数据非空）
     * @return 是否有效
     */
    bool IsValid() const {
        return !raw_frame.empty();
    }

    /**
     * @brief 获取帧分辨率
     * @return 宽度 x 高度
     */
    cv::Size GetFrameSize() const {
        if (raw_frame.empty()) return cv::Size(0, 0);
        return cv::Size(raw_frame.cols, raw_frame.rows);
    }
};

/**
 * @typedef FrameContextPtr
 * @brief FrameContext 智能指针，用于简化管理
 */
using FrameContextPtr = std::shared_ptr<FrameContext>;

/**
 * @brief 创建 FrameContext 智能指针
 * @return 新的 FrameContext
 */
inline FrameContextPtr CreateFrameContext() {
    return std::make_shared<FrameContext>();
}
