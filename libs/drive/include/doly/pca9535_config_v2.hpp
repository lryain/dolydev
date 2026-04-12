/**
 * @file pca9535_config_v2.hpp
 * @brief PCA9535 配置系统 v2.0 - 事件/命令主题可配置 + 调试支持
 */

#pragma once

#include <string>
#include <map>
#include <vector>
#include <nlohmann/json.hpp>

namespace doly {
namespace extio {

using json = nlohmann::json;

/**
 * @brief 单个事件/命令的配置（含调试选项）
 * 
 * enabled:     控制模块是否启用（决定是否进行事件处理和识别）
 * emit_event:  控制是否将事件发布到消息总线
 * debug:       控制是否打印调试日志
 * topic:       主题名称（不含前缀）
 */
struct TopicConfig {
    bool enabled = true;           ///< 模块是否启用（进行事件处理）
    bool emit_event = false;        ///< 是否发布事件到消息总线
    bool debug = false;            ///< 是否打印调试信息
    std::string topic;             ///< 主题名称（不含前缀）
    
    TopicConfig() = default;
    TopicConfig(bool en, const std::string& t, bool dbg = false, bool emit = false) 
        : enabled(en), emit_event(emit), debug(dbg), topic(t) {}
};

/**
 * @brief PCA9535 事件发布配置
 */
struct EventPublishConfig {
    TopicConfig raw_state;
    TopicConfig pin_change;
    TopicConfig touch_gesture;
    TopicConfig touch_history;
    TopicConfig cliff_pattern;
    TopicConfig cliff_history;
};

/**
 * @brief PCA9535 控制命令配置
 */
struct ControlCommandConfig {
    TopicConfig enable_servo_left;
    TopicConfig enable_servo_right;
    TopicConfig enable_tof;
    TopicConfig enable_cliff;
    TopicConfig set_ext_io;
};

/**
 * @brief 完整的 PCA9535 配置 v2.0
 */
struct Pca9535ConfigV2 {
    std::string topic_prefix = "io.pca9535.";          ///< 所有主题前缀
    
    // 全局调试开关
    bool global_debug = false;                         ///< 全局调试开关
    
    // 硬件默认使能状态
    bool enable_servo_left_default = false;
    bool enable_servo_right_default = false;
    bool enable_tof_default = false;
    bool enable_cliff_default = true;
    bool auto_configure_tof = false;                ///< 是否在启动时自动配置 TOF 地址
    
    // 事件发布配置
    EventPublishConfig events;
    
    // 控制命令配置
    ControlCommandConfig commands;
    
    // 触摸手势参数
    struct TouchParams {
        uint32_t single_min_ms = 30;
        uint32_t single_max_ms = 300;
        uint32_t double_interval_ms = 250;
        uint32_t long_press_ms = 600;
    } touch;
    
    // 悬崖传感器参数
    struct CliffParams {
        uint32_t window_ms = 100;
        size_t stable_max_edges = 2;
        size_t line_min_edges = 5;
        uint8_t cliff_duty_threshold = 80;
    } cliff;
    
    // 舵机参数（P1.3 新增）
    struct ServoParams {
        struct ServoSide {
            uint16_t default_angle = 90;           ///< 默认角度（0-180度）
            bool enable_autohold = false;           ///< 是否启用自动保持
            uint32_t autohold_duration_ms = 0;      ///< 自动保持时长（毫秒，0=永不释放）
            
            ServoSide() = default;
            ServoSide(uint16_t angle, bool autohold, uint32_t duration)
                : default_angle(angle), enable_autohold(autohold), autohold_duration_ms(duration) {}
        };
        
        ServoSide left;                             ///< 左舵机配置
        ServoSide right;                            ///< 右舵机配置
    } servo;
    
    /**
     * @brief 从 JSON 加载配置
     */
    static Pca9535ConfigV2 from_json(const json& j);
    
    /**
     * @brief 从 YAML 文件加载配置
     */
    static Pca9535ConfigV2 from_yaml_file(const std::string& filepath);
    
    /**
     * @brief 生成完整主题名称
     */
    std::string full_topic(const std::string& relative_topic) const {
        return topic_prefix + relative_topic;
    }
    
    /**
     * @brief 判断是否应该打印调试信息
     */
    bool should_debug(bool event_debug) const {
        return global_debug || event_debug;
    }
};

} // namespace extio
} // namespace doly
