#ifndef PID_CONTROLLER_HPP
#define PID_CONTROLLER_HPP

#include <chrono>
#include <atomic>

class PIDController {
private:
    // PID参数
    double kp, ki, kd;

    // 状态变量
    double previous_error;
    double integral;
    double setpoint;

    // 时间相关
    std::chrono::steady_clock::time_point last_time;
    double dt;

    // 限制
    double output_min, output_max;
    double integral_min, integral_max;

    // 统计
    std::atomic<int> sample_count;
    std::atomic<double> total_error;

public:
    PIDController(double p = 1.0, double i = 0.0, double d = 0.0);

    // 设置PID参数
    void setPID(double p, double i, double d);
    void setLimits(double min, double max);
    void setIntegralLimits(double min, double max);

    // 计算PID输出
    double compute(double input);
    double compute(double input, double dt);

    // 重置控制器
    void reset();

    // 获取状态
    double getSetpoint() const { return setpoint; }
    void setSetpoint(double sp) { setpoint = sp; }

    // 获取统计信息
    int getSampleCount() const { return sample_count; }
    double getAverageError() const {
        return sample_count > 0 ? total_error / sample_count : 0.0;
    }
};

#endif // PID_CONTROLLER_HPP
