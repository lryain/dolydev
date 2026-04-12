/*

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "drive/pid_controller.hpp"
#include <algorithm>
#include <cmath>

PIDController::PIDController(double p, double i, double d)
    : kp(p), ki(i), kd(d), previous_error(0.0), integral(0.0), setpoint(0.0),
      dt(0.01), output_min(-1.0), output_max(1.0),
      integral_min(-1.0), integral_max(1.0), sample_count(0), total_error(0.0) {
    last_time = std::chrono::steady_clock::now();
}

void PIDController::setPID(double p, double i, double d) {
    kp = p;
    ki = i;
    kd = d;
}

void PIDController::setLimits(double min, double max) {
    output_min = min;
    output_max = max;
}

void PIDController::setIntegralLimits(double min, double max) {
    integral_min = min;
    integral_max = max;
}

double PIDController::compute(double input) {
    auto current_time = std::chrono::steady_clock::now();
    dt = std::chrono::duration<double>(current_time - last_time).count();
    last_time = current_time;

    return compute(input, dt);
}

double PIDController::compute(double input, double delta_time) {
    double error = setpoint - input;

    // 比例项
    double p_term = kp * error;

    // 积分项
    integral += error * delta_time;
    integral = std::max(integral_min, std::min(integral_max, integral));
    double i_term = ki * integral;

    // 微分项
    double derivative = (error - previous_error) / delta_time;
    double d_term = kd * derivative;

    // 计算输出
    double output = p_term + i_term + d_term;
    output = std::max(output_min, std::min(output_max, output));

    // 更新状态
    previous_error = error;

    // 统计
    sample_count++;
    total_error.store(total_error.load() + std::abs(error));

    return output;
}

void PIDController::reset() {
    previous_error = 0.0;
    integral = 0.0;
    sample_count = 0;
    total_error = 0.0;
    last_time = std::chrono::steady_clock::now();
}
