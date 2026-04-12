#pragma once
#include <atomic>
#include <cstdint>
#include <chrono>

namespace doly {
namespace drive {

/**
 * @brief 共享状态结构（Drive → Choreography）
 * 
 * 使用共享内存实现零延迟的传感器状态读取
 * 用于反射弧机制
 */
struct SharedState {
    // ===== 触摸状态 =====
    struct Touch {
        // 原始传感器数据
        std::atomic<bool> touched{false};       // 是否被触摸（低电平有效）
        std::atomic<uint8_t> zone{0};           // 触摸区域 0=无, 1=head, 2=body
        std::atomic<uint64_t> start_time_ms{0}; // 触摸开始时间（毫秒）
        std::atomic<uint64_t> duration_ms{0};   // 持续时间
        
        // 手势识别事件
        std::atomic<uint8_t> gesture_type{0};   // 0=NONE, 1=TAP, 2=DOUBLE_TAP, 3=LONG_PRESS
        std::atomic<uint64_t> gesture_time_ms{0}; // 手势识别时间戳
    } touch;
    
    // ===== 悬崖状态 =====
    struct Cliff {
        // 原始传感器数据
        std::atomic<bool> left_triggered{false};   // 左侧悬崖触发
        std::atomic<bool> right_triggered{false};  // 右侧悬崖触发
        std::atomic<uint16_t> left_value{0};       // 左侧ADC值
        std::atomic<uint16_t> right_value{0};      // 右侧ADC值
        std::atomic<uint64_t> trigger_time_ms{0};  // 触发时间
        
        // 模式识别事件
        std::atomic<uint8_t> pattern_type{0};      // 0=NONE, 1=STABLE, 2=LINE, 3=CLIFF
        std::atomic<uint8_t> sensor{0};            // 1=left, 2=right, 3=both
    } cliff;
    
    // ===== TOF 传感器状态 =====
    struct TOF {
        // 原始传感器数据
        std::atomic<uint16_t> left_mm{0};       // 左侧距离（毫米）255=无效
        std::atomic<uint16_t> right_mm{0};      // 右侧距离（毫米）255=无效
        std::atomic<bool> left_valid{false};    // 左侧数据有效
        std::atomic<bool> right_valid{false};   // 右侧数据有效
        std::atomic<uint64_t> update_time_ms{0};
        
        // 事件识别
        std::atomic<uint8_t> event_type{0};     // 0=NONE, 1=OBSTACLE_NEAR(<150mm), 2=OBSTACLE_FAR(150-500mm), 3=CLEAR
        std::atomic<uint16_t> min_distance_mm{9999}; // 最小距离（毫米）
        std::atomic<bool> obstacle_detected{false};  // 障碍物标志（<150mm）
    } tof;
    
    // ===== IMU 状态 =====
    struct IMU {
        // 原始传感器数据
        std::atomic<float> ax{0.0f};   // 加速度 X (g)
        std::atomic<float> ay{0.0f};   // 加速度 Y (g)
        std::atomic<float> az{0.0f};   // 加速度 Z (g)
        std::atomic<float> gx{0.0f};   // 角速度 X (°/s)
        std::atomic<float> gy{0.0f};   // 角速度 Y (°/s)
        std::atomic<float> gz{0.0f};   // 角速度 Z (°/s)
        
        // AHRS 姿态数据（软件解算）
        std::atomic<float> roll{0.0f};   // 横滚角（°）[-180, 180]
        std::atomic<float> pitch{0.0f};  // 俯仰角（°）[-180, 180]
        std::atomic<float> yaw{0.0f};    // 偏航角（°）[-180, 180]
        std::atomic<uint8_t> orientation{0};  // 0=Unknown, 1=X-UP, 2=X-DOWN, 3=Y-UP, 4=Y-DOWN, 5=Z-UP, 6=Z-DOWN
        
        // MLC 手势识别
        std::atomic<uint8_t> gesture{0};        // 0=None, 1=Nod, 2=Shake, 3=Swing, 4=Walk
        std::atomic<uint64_t> gesture_time_ms{0};  // 手势检测时间戳
        
        // 事件检测
        std::atomic<bool> shock_detected{false};  // 冲击检测（Shake/Swing）
        std::atomic<uint64_t> update_time_ms{0};
    } imu;
    
    // ===== 电源状态 =====
    struct Power {
        std::atomic<float> voltage{0.0f};      // 电压 (V)
        std::atomic<float> current{0.0f};      // 电流 (A)
        std::atomic<uint8_t> percentage{100};  // 电量百分比
        std::atomic<bool> low_battery{false};  // 低电量警告
        std::atomic<uint64_t> update_time_ms{0};
    } power;
    
    // ===== 眼睛状态 (EyeEngine) =====
    struct Eye {
        // 当前情绪 (0-7)
        std::atomic<uint8_t> emotion{0};        // 0=neutral, 1=happy, 2=sad, 3=angry, 4=surprised, 5=sleepy, 6=blink, 7=love
        std::atomic<float> intensity{0.5f};     // 情绪强度 [0.0, 1.0]
        
        // 当前表情 (0-4)
        std::atomic<uint8_t> expression{0};     // 0=normal, 1=squint, 2=wide, 3=wink_left, 4=wink_right
        
        // 凝视状态 (0-9)
        std::atomic<uint8_t> gaze_type{0};      // 0=center, 1=up, 2=down, 3=left, 4=right, 5=up_left, 6=up_right, 7=down_left, 8=down_right, 9=follow
        std::atomic<float> gaze_x{0.0f};        // 凝视目标 X 坐标 [-1.0, 1.0]
        std::atomic<float> gaze_y{0.0f};        // 凝视目标 Y 坐标 [-1.0, 1.0]
        
        // Widget 显示状态 (0-3)
        std::atomic<uint8_t> left_slot_state{0};   // 0=rendering, 1=showing_widget, 2=transitioning, 3=error
        std::atomic<uint8_t> right_slot_state{0};  // 0=rendering, 1=showing_widget, 2=transitioning, 3=error
        std::atomic<uint8_t> left_widget_id{0};    // 左眼 Widget ID (0=none, 1=clock, 2=weather, 3=timer, 4=custom)
        std::atomic<uint8_t> right_widget_id{0};   // 右眼 Widget ID (0=none, 1=clock, 2=weather, 3=timer, 4=custom)
        
        // LCD 启用状态
        std::atomic<bool> left_lcd_enabled{true};  // 左眼 LCD 启用
        std::atomic<bool> right_lcd_enabled{true}; // 右眼 LCD 启用
        
        // 动画状态
        std::atomic<bool> is_blinking{false};      // 正在眨眼
        std::atomic<bool> is_transitioning{false}; // 正在转场
        
        // 性能指标
        std::atomic<uint16_t> fps{0};              // 当前帧率
        std::atomic<uint64_t> frame_count{0};      // 总帧数
        
        // 元数据
        std::atomic<uint64_t> update_time_ms{0};   // 最后更新时间
        std::atomic<uint32_t> sequence{0};         // 序列号（用于检测更新）
    } eye;
    
    // ===== 元数据 =====
    uint32_t magic = 0xD014FEED;  // 魔数，用于验证
    uint32_t version = 2;         // 版本号（更新为2，添加了眼睛状态）
    
    // 获取当前时间戳（毫秒）
    static uint64_t getCurrentTimeMs() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
    }
};

// 共享内存名称
constexpr const char* SHARED_STATE_NAME = "/doly_shared_state";
constexpr size_t SHARED_STATE_SIZE = sizeof(SharedState);

} // namespace drive
} // namespace doly
