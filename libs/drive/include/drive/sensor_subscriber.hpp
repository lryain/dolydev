/**
 * @file sensor_subscriber.hpp
 * @brief 传感器数据订阅（TOF、IMU）
 * 
 * 职责：
 * - 订阅 TOF 距离传感器数据
 * - 订阅 IMU 姿态数据与手势事件
 * - 更新 SharedState
 * - 按配置打印日志
 */

#pragma once

#include "drive/shared_state.hpp"
#include "drive/filters/filter_base.hpp"
#include <atomic>
#include <thread>
#include <memory>

namespace doly::drive {

/**
 * @brief 传感器日志配置
 */
struct SensorLogConfig {
    struct Config {
        bool enabled = true;
        bool debug = true;
        bool debug_raw = false;
        uint64_t log_interval_ms = 1000;
        uint64_t last_log_time_ms = 0;
        uint64_t last_raw_time_ms = 0;
    };
    
    Config tof;
    Config ahrs;
    Config gesture;
    Config power;
    
    bool should_log(Config& cfg);
    bool should_log_raw(Config& cfg);
};

/**
 * @brief 传感器订阅管理器
 */
class SensorSubscriber {
public:
    SensorSubscriber(SharedState* shared_state, const SensorLogConfig& config);
    ~SensorSubscriber();
    
    // 启动订阅线程
    bool start();
    
    // 停止订阅线程
    void stop();
    
    // 是否在运行
    bool is_running() const { return running_.load(); }
    
private:
    SharedState* shared_state_;
    SensorLogConfig config_;
    std::atomic<bool> running_{false};
    
    // 采样率限制器
    filters::SampleRateLimiter tof_limiter_{20.0f};
    filters::SampleRateLimiter imu_ahrs_limiter_{50.0f};
    
    // TOF 滤波器
    std::unique_ptr<filters::ChainFilter> tof_left_filter_;
    std::unique_ptr<filters::ChainFilter> tof_right_filter_;
    filters::OutlierDetector tof_left_detector_;
    filters::OutlierDetector tof_right_detector_;
    
    // IMU 滤波器
    std::unique_ptr<filters::KalmanFilter1D> imu_roll_filter_;
    std::unique_ptr<filters::KalmanFilter1D> imu_pitch_filter_;
    std::unique_ptr<filters::KalmanFilter1D> imu_yaw_filter_;
    filters::OutlierDetector imu_detector_;

    // 电源滤波器
    std::unique_ptr<filters::MovingAverageFilter> power_voltage_filter_;
    std::unique_ptr<filters::MovingAverageFilter> power_current_filter_;
    filters::SampleRateLimiter power_limiter_{5.0f};

    std::thread tof_thread_;
    std::thread imu_thread_;
    std::thread power_thread_;
    
    void tof_subscriber_loop();
    void imu_subscriber_loop();
    void power_subscriber_loop();

    // 初始化滤波器方法
    void init_filters();
};

} // namespace doly::drive
