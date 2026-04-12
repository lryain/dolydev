/**
 * @file filter_base.hpp
 * @brief 传感器滤波器基类定义
 * 
 * 提供统一的滤波器接口，支持：
 * - 移动平均滤波
 * - 中值滤波
 * - 一阶低通滤波
 * - 一维卡尔曼滤波
 */

#pragma once

#include <vector>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace doly::drive::filters {

/**
 * @brief 滤波器基类
 */
class FilterBase {
public:
    virtual ~FilterBase() = default;
    
    /**
     * @brief 更新滤波器（核心接口）
     * @param raw_value 原始传感器值
     * @return 滤波后的值
     */
    virtual float update(float raw_value) = 0;
    
    /**
     * @brief 重置滤波器状态
     */
    virtual void reset() = 0;
    
    /**
     * @brief 滤波器是否已初始化
     */
    virtual bool is_initialized() const = 0;
    
    /**
     * @brief 获取滤波器名称
     */
    virtual const char* name() const = 0;
};

/**
 * @brief 移动平均滤波器
 * 
 * 适用于: 需要强力平滑的信号（电压、电流）
 * 优点: 简单高效、平滑效果好
 * 缺点: 响应慢、延迟大
 */
class MovingAverageFilter : public FilterBase {
public:
    /**
     * @param window_size 窗口大小（样本数量）
     */
    explicit MovingAverageFilter(size_t window_size = 10)
        : window_size_(window_size) {
        buffer_.reserve(window_size);
    }
    
    float update(float raw_value) override {
        buffer_.push_back(raw_value);
        if (buffer_.size() > window_size_) {
            buffer_.erase(buffer_.begin());
        }
        
        float sum = 0.0f;
        for (float val : buffer_) {
            sum += val;
        }
        
        filtered_value_ = sum / buffer_.size();
        initialized_ = buffer_.size() >= window_size_;
        return filtered_value_;
    }
    
    void reset() override {
        buffer_.clear();
        initialized_ = false;
        filtered_value_ = 0.0f;
    }
    
    bool is_initialized() const override {
        return initialized_;
    }
    
    const char* name() const override {
        return "MovingAverage";
    }
    
    float get_value() const { return filtered_value_; }
    
private:
    size_t window_size_;
    std::vector<float> buffer_;
    float filtered_value_ = 0.0f;
    bool initialized_ = false;
};

/**
 * @brief 中值滤波器
 * 
 * 适用于: 含有脉冲毛刺的信号（TOF距离、电流尖峰）
 * 优点: 去毛刺效果极好、保留边缘
 * 缺点: 排序开销、延迟
 */
class MedianFilter : public FilterBase {
public:
    /**
     * @param window_size 窗口大小（建议奇数）
     */
    explicit MedianFilter(size_t window_size = 5)
        : window_size_(window_size) {
        buffer_.reserve(window_size);
    }
    
    float update(float raw_value) override {
        buffer_.push_back(raw_value);
        if (buffer_.size() > window_size_) {
            buffer_.erase(buffer_.begin());
        }
        
        // 复制并排序
        std::vector<float> sorted = buffer_;
        std::sort(sorted.begin(), sorted.end());
        
        // 取中值
        size_t mid = sorted.size() / 2;
        if (sorted.size() % 2 == 0 && sorted.size() > 1) {
            filtered_value_ = (sorted[mid - 1] + sorted[mid]) / 2.0f;
        } else {
            filtered_value_ = sorted[mid];
        }
        
        initialized_ = buffer_.size() >= window_size_;
        return filtered_value_;
    }
    
    void reset() override {
        buffer_.clear();
        initialized_ = false;
        filtered_value_ = 0.0f;
    }
    
    bool is_initialized() const override {
        return initialized_;
    }
    
    const char* name() const override {
        return "Median";
    }
    
    float get_value() const { return filtered_value_; }
    
private:
    size_t window_size_;
    std::vector<float> buffer_;
    float filtered_value_ = 0.0f;
    bool initialized_ = false;
};

/**
 * @brief 一阶低通滤波器（指数加权移动平均）
 * 
 * 适用于: 需要快速响应+适度平滑的信号（TOF距离、姿态角）
 * 优点: 计算量极小、无延迟、可调参数简单
 * 缺点: 对毛刺敏感（需配合中值滤波）
 * 
 * 公式: y(t) = α*x(t) + (1-α)*y(t-1)
 * α: 平滑系数 [0,1]，越小越平滑，越大响应越快
 */
class LowPassFilter : public FilterBase {
public:
    /**
     * @param alpha 平滑系数 [0.0, 1.0]
     *              0.1 = 强平滑慢响应
     *              0.3 = 平衡
     *              0.7 = 弱平滑快响应
     */
    explicit LowPassFilter(float alpha = 0.3f)
        : alpha_(alpha) {
        // 限制范围
        if (alpha_ < 0.0f) alpha_ = 0.0f;
        if (alpha_ > 1.0f) alpha_ = 1.0f;
    }
    
    float update(float raw_value) override {
        if (!initialized_) {
            filtered_value_ = raw_value;
            initialized_ = true;
        } else {
            filtered_value_ = alpha_ * raw_value + (1.0f - alpha_) * filtered_value_;
        }
        return filtered_value_;
    }
    
    void reset() override {
        initialized_ = false;
        filtered_value_ = 0.0f;
    }
    
    bool is_initialized() const override {
        return initialized_;
    }
    
    const char* name() const override {
        return "LowPass";
    }
    
    float get_value() const { return filtered_value_; }
    
    /**
     * @brief 从时间常数计算 alpha
     * @param dt 采样周期（秒）
     * @param tau 时间常数（秒），表示信号衰减到36.8%的时间
     * @return alpha = dt / (tau + dt)
     */
    static float alpha_from_tau(float dt, float tau) {
        return dt / (tau + dt);
    }
    
private:
    float alpha_;
    float filtered_value_ = 0.0f;
    bool initialized_ = false;
};

/**
 * @brief 一维卡尔曼滤波器
 * 
 * 适用于: 有明确物理模型的信号（姿态角、速度、位置）
 * 优点: 理论最优估计、动态调整增益
 * 缺点: 需要调参（Q、R）、计算量稍大
 * 
 * 状态方程: x(t) = x(t-1) + w(t)     w ~ N(0, Q)
 * 观测方程: z(t) = x(t) + v(t)       v ~ N(0, R)
 */
class KalmanFilter1D : public FilterBase {
public:
    /**
     * @param Q 过程噪声协方差（系统模型不确定性）
     *          值越大，信任测量值越多
     * @param R 测量噪声协方差（传感器噪声）
     *          值越大，信任预测值越多
     * @param initial_P 初始误差协方差
     */
    KalmanFilter1D(float Q = 1.0f, float R = 4.0f, float initial_P = 1.0f)
        : Q_(Q), R_(R), P_(initial_P) {}
    
    float update(float measurement) override {
        if (!initialized_) {
            // 首次初始化：直接使用测量值
            x_ = measurement;
            initialized_ = true;
            return x_;
        }
        
        // 预测步骤
        // x_pred = x (常量模型：状态不变)
        // P_pred = P + Q
        P_ = P_ + Q_;
        
        // 更新步骤
        // K = P_pred / (P_pred + R)
        float K = P_ / (P_ + R_);
        
        // x = x_pred + K * (z - x_pred)
        x_ = x_ + K * (measurement - x_);
        
        // P = (1 - K) * P_pred
        P_ = (1.0f - K) * P_;
        
        return x_;
    }
    
    void reset() override {
        initialized_ = false;
        x_ = 0.0f;
        P_ = 1.0f;
    }
    
    bool is_initialized() const override {
        return initialized_;
    }
    
    const char* name() const override {
        return "Kalman1D";
    }
    
    float get_value() const { return x_; }
    float get_variance() const { return P_; }
    float get_kalman_gain() const { return P_ / (P_ + R_); }
    
    /**
     * @brief 设置过程噪声（运行时调整）
     */
    void set_Q(float Q) { Q_ = Q; }
    
    /**
     * @brief 设置测量噪声（运行时调整）
     */
    void set_R(float R) { R_ = R; }
    
private:
    float Q_;              // 过程噪声协方差
    float R_;              // 测量噪声协方差
    float x_ = 0.0f;       // 状态估计
    float P_ = 1.0f;       // 误差协方差
    bool initialized_ = false;
};

/**
 * @brief 组合滤波器（链式滤波）
 * 
 * 示例: MedianFilter(5) -> LowPassFilter(0.3)
 * 先用中值滤波去毛刺，再用低通滤波平滑
 */
class ChainFilter : public FilterBase {
public:
    ChainFilter() = default;
    
    /**
     * @brief 添加滤波器到链中
     */
    void add_filter(FilterBase* filter) {
        filters_.push_back(filter);
    }
    
    float update(float raw_value) override {
        float value = raw_value;
        for (auto* filter : filters_) {
            value = filter->update(value);
        }
        return value;
    }
    
    void reset() override {
        for (auto* filter : filters_) {
            filter->reset();
        }
    }
    
    bool is_initialized() const override {
        for (auto* filter : filters_) {
            if (!filter->is_initialized()) {
                return false;
            }
        }
        return !filters_.empty();
    }
    
    const char* name() const override {
        return "ChainFilter";
    }
    
private:
    std::vector<FilterBase*> filters_;
};

/**
 * @brief 异常值检测器
 * 
 * 功能：
 * - 检测无效值（如 TOF 的 255）
 * - 检测突变值（单次变化过大）
 * - 检测超出范围的值
 */
class OutlierDetector {
public:
    struct Config {
        bool enabled = true;
        float invalid_value = -1.0f;        // 无效值标记（-1表示不检测）
        float max_change = -1.0f;           // 最大单次变化（-1表示不检测）
        float min_valid = -INFINITY;        // 最小有效值
        float max_valid = INFINITY;         // 最大有效值
        bool use_previous = true;           // 检测到异常时使用上一次有效值
    };
    
    explicit OutlierDetector(const Config& config)
        : config_(config) {}
    
    OutlierDetector() : config_(Config()) {}
    
    /**
     * @brief 检测是否为异常值
     * @param value 当前值
     * @param filtered_output 输出值（如果是异常，输出上次有效值）
     * @return true=异常，false=正常
     */
    bool detect(float value, float& filtered_output) {
        bool is_outlier = false;
        
        if (!config_.enabled) {
            filtered_output = value;
            last_valid_value_ = value;
            return false;
        }
        
        // 检测无效值
        if (config_.invalid_value >= 0 && 
            std::fabs(value - config_.invalid_value) < 0.001f) {
            is_outlier = true;
        }
        
        // 检测超出范围
        if (value < config_.min_valid || value > config_.max_valid) {
            is_outlier = true;
        }
        
        // 检测突变
        if (has_valid_value_ && config_.max_change > 0) {
            float change = std::fabs(value - last_valid_value_);
            if (change > config_.max_change) {
                is_outlier = true;
            }
        }
        
        // 输出处理
        if (is_outlier) {
            outlier_count_++;
            if (config_.use_previous && has_valid_value_) {
                filtered_output = last_valid_value_;
            } else {
                filtered_output = value;  // 无上次有效值，仍输出当前值
            }
        } else {
            filtered_output = value;
            last_valid_value_ = value;
            has_valid_value_ = true;
        }
        
        return is_outlier;
    }
    
    void reset() {
        has_valid_value_ = false;
        last_valid_value_ = 0.0f;
        outlier_count_ = 0;
    }
    
    uint32_t get_outlier_count() const { return outlier_count_; }
    
private:
    Config config_;
    bool has_valid_value_ = false;
    float last_valid_value_ = 0.0f;
    uint32_t outlier_count_ = 0;
};

/**
 * @brief 采样率限制器
 * 
 * 功能：控制传感器数据处理频率，降低 CPU 占用
 */
class SampleRateLimiter {
public:
    /**
     * @param target_hz 目标采样率（Hz）
     */
    explicit SampleRateLimiter(float target_hz = 20.0f) {
        set_rate(target_hz);
    }
    
    /**
     * @brief 设置采样率
     */
    void set_rate(float target_hz) {
        if (target_hz > 0) {
            interval_us_ = static_cast<uint64_t>(1000000.0f / target_hz);
        } else {
            interval_us_ = 0;  // 不限制
        }
    }
    
    /**
     * @brief 检查是否应该采样
     * @param current_time_us 当前时间（微秒）
     * @return true=应该采样，false=跳过
     */
    bool should_sample(uint64_t current_time_us) {
        if (interval_us_ == 0) {
            return true;  // 不限制
        }
        
        if (current_time_us - last_sample_time_us_ >= interval_us_) {
            last_sample_time_us_ = current_time_us;
            sample_count_++;
            return true;
        }
        
        skipped_count_++;
        return false;
    }
    
    void reset() {
        last_sample_time_us_ = 0;
        sample_count_ = 0;
        skipped_count_ = 0;
    }
    
    uint64_t get_sample_count() const { return sample_count_; }
    uint64_t get_skipped_count() const { return skipped_count_; }
    
private:
    uint64_t interval_us_ = 50000;  // 默认 20Hz
    uint64_t last_sample_time_us_ = 0;
    uint64_t sample_count_ = 0;
    uint64_t skipped_count_ = 0;
};

} // namespace doly::drive::filters
