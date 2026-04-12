#pragma once
#include "ServoMotor.h"  // 官方 Doly API
#include <mutex>
#include <map>
#include <thread>
#include <atomic>
#include <queue>
#include <functional>
#include <condition_variable>

namespace doly {
namespace drive {

// 回调函数类型：当舵机需要使能或释放时触发
using ServoPowerCallback = std::function<void(ServoChannel, bool)>;

// 插值类型
enum class EasingType {
    Linear,
    EaseInOutSine
};

// 舵机状态
struct ServoState {
    float start_angle = 90.0f;
    float target_angle = 90.0f;
    float current_angle = 90.0f;
    uint8_t speed = 50;
    bool swing_after_move = false;          // 先移动再摆动的流程
    float swing_pending_center = 90.0f;     // 摆动中心（调用前的角度）
    float swing_pending_amplitude = 0.0f;   // 摆动总幅度（从中心往返）
    uint8_t swing_pending_speed = 50;       // 摆动中的速度
    int swing_pending_count = 0;            // 摆动次数（-1 无限）
    
    // 动画控制
    long long start_time_ms = 0;
    long long duration_ms = 0;
    EasingType easing = EasingType::Linear;
    bool active = false; // 是否正在运动

    // 摆动模式控制
    bool is_swinging = false;
    float swing_min = 0.0f;
    float swing_max = 0.0f;
    int swing_count = 0; // 剩余次数，-1 表示无限
    int swing_direction = 1; // 1: min->max, -1: max->min
    
    // 自动保持/释放控制
    bool auto_hold_enabled = false;          // 是否启用自动释放
    long long auto_hold_duration_ms = 3000;  // 保持时长（默认3秒）
    long long motion_complete_time_ms = 0;   // 运动完成时间戳
};

/**
 * @brief 舵机控制器（增强版）
 * 
 * 封装官方 ServoMotor 库，提供统一的接口给 Drive 服务
 * 增强功能：平滑插值、多舵机同步、动作序列、摆动模式
 */
class ServoController {
public:
    ServoController() = default;
    ~ServoController(); // 需要析构函数来停止线程
    
    /**
     * @brief 初始化舵机模块
     * 
     * 内部调用 ServoMotor::Init() 和 setup()
     * ⚠️ 会自动初始化 PCA9685 为 50Hz，必须在 MotorController 之前调用
     * 
     * @param initial_angles 初始角度映射，将立即写入硬件，防止启动抖动
     * @param auto_hold_modes 映射通道是否启用自动释放
     * @return true 成功，false 失败
     */
    bool Init(const std::map<ServoChannel, float>& initial_angles,
              const std::map<ServoChannel, bool>& auto_hold_modes);
    
    /**
     * @brief 设置舵机角度
     * 
     * @param channel 舵机通道（SERVO_LEFT, SERVO_RIGHT）
     * @param angle 目标角度（0-210°）
     * @param speed 运动速度（0-100，默认 50）
     * @return true 成功，false 失败
     */
    bool SetAngle(ServoChannel channel, float angle, uint8_t speed = 50);
    
    /**
     * @brief 平滑设置舵机角度（带持续时间）
     * 
     * @param channel 舵机通道
     * @param angle 目标角度
     * @param duration_ms 运动持续时间（毫秒）
     * @return true 成功，false 失败
     */
    bool SetAngleSmooth(ServoChannel channel, float angle, int duration_ms);
    
    /**
     * @brief 多舵机同步移动
     * @param targets key: ServoChannel, value: 目标角度
     * @param speed 相对速度 (1-100)，我们会根据最大移动幅度计算持续时间
     */
    void MoveMulti(const std::map<ServoChannel, float>& targets, uint8_t speed = 50);

    /**
     * @brief 多舵机指定时间移动
     */
    void MoveMultiDuration(const std::map<ServoChannel, float>& targets, int duration_ms);
    
    /**
    * @brief 先以指定速度移动到目标角度，再以之前的角度为中心进行固定幅度摆动
     * @param channel 舵机通道
     * @param target_angle 首先移动的角度
     * @param approach_speed 移动到目标时的速度
    * @param swing_amplitude 摆动半幅度（角度中心两侧的最大偏移）
     * @param swing_speed 摆动过程中的速度
     * @param count 摆动次数，-1 表示无限
     */
    void ServoSwingOf(ServoChannel channel, float target_angle, uint8_t approach_speed, float swing_amplitude, uint8_t swing_speed, int count = -1);

    /**
     * @brief 开始摆动模式
     * @param channel 通道
     * @param min_angle 最小角度
     * @param max_angle 最大角度
     * @param duration_one_way 单程耗时(ms)
     * @param count 摆动次数(0或-1为无限)
     */
    void StartSwing(ServoChannel channel, float min_angle, float max_angle, int duration_one_way, int count);

    /**
     * @brief 停止舵机（停止动画）
     */
    bool Stop(ServoChannel channel);
    
    /**
     * @brief 停止所有舵机
     */
    void StopAll();
    
    /**
     * @brief 设置舵机自动保持/释放参数
     * @param channel 舵机通道
     * @param enabled 是否启用自动保持
     * @param hold_duration_ms 保持时长（毫秒）
     */
    void SetAutoHold(ServoChannel channel, bool enabled, long long hold_duration_ms);
    
    /**
     * @brief 设置舵机电源控制回调
     * @param cb 回调函数 (channel, enable)
     */
    void SetPowerCallback(ServoPowerCallback cb);

    /**
     * @brief 检查模块是否就绪
     * 
     * @return true 就绪，false 未就绪
     */
    bool IsActive() const;

    
    // ========== 新增动作函数 ==========
    
    /**
     * @brief 单臂举哑铃动作
     * @param channel 舵机通道(SERVO_LEFT/SERVO_RIGHT)
     * @param weight 哑铃重量(0-100,影响速度和幅度)
     * @param reps 举重次数
     */
    void LiftDumbbell(ServoChannel channel, float weight, int reps);

    /**
     * @brief 双臂交替举哑铃舞蹈动作
     * @param weight 哑铃重量(0-100,影响速度和幅度)
     * @param duration_sec 持续时间(秒)
     */
    void DumbbellDance(float weight, float duration_sec);

    /**
     * @brief 挥舞旗子动作
     * @param channel 舵机通道
     * @param flag_weight 旗子重量(0-100,影响挥舞速度和幅度)
     * @param wave_count 挥舞次数
     */
    void WaveFlag(ServoChannel channel, float flag_weight, int wave_count);

    /**
     * @brief 打鼓动作
     * @param channel 舵机通道
     * @param stick_weight 鼓棒重量(0-100,影响速度和力度)
     * @param beat_count 打鼓次数
     */
    void BeatDrum(ServoChannel channel, float stick_weight, int beat_count);

    /**
     * @brief 单臂划桨动作
     * @param channel 舵机通道
     * @param paddle_weight 桨重量(0-100)
     * @param stroke_count 划桨次数
     */
    void PaddleRow(ServoChannel channel, float paddle_weight, int stroke_count);

    /**
     * @brief 双臂同步划桨动作
     * @param paddle_weight 桨重量(0-100)
     * @param stroke_count 划桨次数
     */
    void DualPaddleRow(float paddle_weight, int stroke_count);
    
private:
    // 内部控制循环
    void Loop();
    // 映射并写硬件
    void WriteHardware(ServoChannel channel, float logical_angle, uint8_t speed);
    // 根据角度差和速度计算持续时间
    int DurationForSpeed(float angle_diff, uint8_t speed) const;
    // 加载配置
    void LoadConfig();
    

private:
    bool initialized_ = false;
    std::mutex mutex_; // 保护配置数据
    
    std::mutex state_mutex_; // 保护 state_ map
    std::map<ServoChannel, ServoState> states_;

    // 线程相关
    std::thread loop_thread_;
    std::atomic<bool> running_{false};
    
    // 舵机角度偏移量
    float left_offset_ = 0.0f;
    float right_offset_ = 0.0f;
    
    // 90度校准偏移量（从配置文件读取）
    float center_offset_left_ = 0.0f;
    float center_offset_right_ = 0.0f;
    
    // 动作回调
    ServoPowerCallback power_cb_;
    
    // 舵机最大角度（从配置文件读取，用于角度映射）
    int left_max_angle_ = SERVO_ARM_MAX_ANGLE;
    int right_max_angle_ = SERVO_ARM_MAX_ANGLE;
};

} // namespace drive
} // namespace doly


