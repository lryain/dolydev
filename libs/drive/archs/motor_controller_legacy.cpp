/*

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "drive/motor_controller.hpp"
#include <cstring>
#include <cmath>
#include <filesystem>
#include <iomanip>

MotorController::MotorController(const std::string& i2c_dev, int addr, const std::string& config_file)
    : device_path(i2c_dev), pca9685_addr(addr), continuous_mode(false),
      auto_stop_timeout(2.0f), auto_stop_cancel(false), running(true), 
      left_speed(0.0f), right_speed(0.0f),
      left_motor_reverse(false), right_motor_reverse(false),
      left_pwm_compensation(1.0f), right_pwm_compensation(1.0f),
            left_pid(1.0, 0.0, 0.0), right_pid(1.0, 0.0, 0.0), pid_enabled(false),
      left_encoder_position(0.0), right_encoder_position(0.0),
      left_encoder(nullptr), right_encoder(nullptr), encoders_enabled(false),
      left_encoder_offset(0), right_encoder_offset(0),  // 初始化偏移量
      safety_monitor(nullptr), safety_enabled(false), current_monitoring_enabled(false) {
    i2c_fd = -1;
        // 默认缓启动 0.2 秒
        ramp_time_seconds = 0.2f;

    // 初始化编码器配置（默认值）
    encoder_config_.enable_encoder = false;
    encoder_config_.pulses_per_revolution = 12;
    encoder_config_.wheel_diameter_cm = 3.0f;
    encoder_config_.wheel_circumference_cm = 9.4248f;
    encoder_config_.cm_per_pulse = 0.7854f;
    encoder_config_.axle_width_cm = 10.5f;
    encoder_config_.turn_compensation_factor = 1.0f;
    encoder_config_.position_tolerance = 2;
    encoder_config_.angle_tolerance_deg = 2.0f;
    encoder_config_.enable_pid = false;
    encoder_config_.debug_encoder = false;
    encoder_config_.debug_pulses = false;
    
    // M2 死区参数初始化
    encoder_config_.pwm_deadzone = 0.1f;
    encoder_config_.pwm_min_active = 0.1f;
    encoder_config_.pwm_max = 1.0f;
    
    // 初始化 PID 配置（默认值）
    position_pid_config_.kp = 0.5f;
    position_pid_config_.ki = 0.05f;
    position_pid_config_.kd = 0.1f;
    position_pid_config_.ki_limit = 20.0f;
    position_pid_config_.output_min = 10.0f;
    position_pid_config_.output_max = 50.0f;
    position_pid_config_.control_period_ms = 50;
    
    // 初始化 PID 状态
    left_pid_state_.reset();
    right_pid_state_.reset();

    // 初始化时自动读取电机方向配置
    if (!config_file.empty()) {
        // 如果提供了配置文件路径，直接使用
        float ramp_out = 0.0f;
        loadMotorConfig(left_motor_reverse, right_motor_reverse, config_file, &ramp_out);
        if (ramp_out > 0.0f) {
            ramp_time_seconds = ramp_out;
            try {
                std::filesystem::path pp(config_file);
                std::cout << "⚙️ CLI: ramp_time 已设置为 " << ramp_out << " 秒 SRC: [" << pp.filename().string() << "]" << std::endl;
            } catch (...) {
                std::cout << "⚙️ CLI: ramp_time 已设置为 " << ramp_out << " 秒 SRC: [" << config_file << "]" << std::endl;
            }
        }
        // 加载编码器配置和PWM补偿值
        load_encoder_config(config_file);
        load_pwm_compensation(config_file);
    } else {
        // 否则使用自动搜索
        // float ramp_out = 0.0f;
        (this->*static_cast<bool(MotorController::*)(bool&, bool&)>(&MotorController::loadMotorConfig))(left_motor_reverse, right_motor_reverse);
        // 尝试通过静态加载从搜索路径读取 ramp_time
        std::vector<std::string> search_paths = {"/home/pi/dolydev/config/motor_config.ini"};
        for (const auto& p : search_paths) {
            float r = 0.0f;
            if (loadMotorConfig(left_motor_reverse, right_motor_reverse, p, &r)) {
                if (r > 0.0f) ramp_time_seconds = r;
            }
            // 同时加载编码器配置和PWM补偿值
            load_encoder_config(p);
            load_pwm_compensation(p);
        }
    }
}

// Delegating constructor to support two-argument overload
MotorController::MotorController(const std::string& i2c_dev, int addr)
    : MotorController(i2c_dev, addr, std::string()) {}

bool MotorController::loadMotorConfig(bool& left_reverse, bool& right_reverse, const std::string& config_file, float* ramp_time_out) {
    left_reverse = false;
    right_reverse = false;

    std::ifstream config_stream(config_file);
    if (!config_stream.is_open()) {
        std::cout << "配置文件 " << config_file << " 不存在，使用默认配置" << std::endl;
        return false;
    }

    std::string line;
    bool in_motor_section = false;

    while (std::getline(config_stream, line)) {
        // 移除注释和空白字符
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }

        // 移除前后空白
        line.erase(line.begin(), std::find_if(line.begin(), line.end(), [](unsigned char ch) {
            return !std::isspace(ch);
        }));
        line.erase(std::find_if(line.rbegin(), line.rend(), [](unsigned char ch) {
            return !std::isspace(ch);
        }).base(), line.end());

        if (line.empty()) continue;

        // 检查节
        if (line == "[motor]") {
            in_motor_section = true;
            continue;
        }

        if (in_motor_section) {
            // 解析配置项
            size_t equal_pos = line.find('=');
            if (equal_pos != std::string::npos) {
                std::string key = line.substr(0, equal_pos);
                std::string value = line.substr(equal_pos + 1);

                // 移除前后空白
                key.erase(key.begin(), std::find_if(key.begin(), key.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
                key.erase(std::find_if(key.rbegin(), key.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(), key.end());

                value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }));
                value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char ch) {
                    return !std::isspace(ch);
                }).base(), value.end());

                if (key == "left_reverse") {
                    left_reverse = (value == "true" || value == "1");
                } else if (key == "right_reverse") {
                    right_reverse = (value == "true" || value == "1");
                } else if (key == "ramp_time") {
                    // 可选的缓启动时间（秒）
                    try {
                        float rt = std::stof(value);
                        if (ramp_time_out) *ramp_time_out = rt;
                    } catch (...) {
                        // ignore parse errors
                    }
                }
            }
        }
    }

    config_stream.close();

    std::cout << "电机方向配置已加载: 左电机" << (left_reverse ? "反转" : "正转")
              << ", 右电机" << (right_reverse ? "反转" : "正转") << std::endl;

    return true;
}

bool MotorController::loadMotorConfig(bool& left_reverse, bool& right_reverse) {
    // 按优先级搜索配置文件路径
    std::vector<std::string> search_paths = {
        "/home/pi/dolydev/config/motor_config.ini"
    };

    for (const auto& p : search_paths) {
        if (p.empty()) continue;
        float r = 0.0f;
        if (loadMotorConfig(left_reverse, right_reverse, p, &r)) {
            if (r > 0.0f) {
                ramp_time_seconds = r;
                try {
                    std::filesystem::path pp(p);
                    std::cout << "⚙️ CLI: ramp_time 已设置为 " << r << " 秒 SRC: [" << pp.filename().string() << "]" << std::endl;
                } catch (...) {
                    std::cout << "⚙️ CLI: ramp_time 已设置为 " << r << " 秒 SRC: [" << p << "]" << std::endl;
                }
            }
            std::cout << "配置文件已从 " << p << " 加载" << std::endl;
            return true;
        }
    }

    std::cout << "未找到配置文件，使用默认配置" << std::endl;
    left_reverse = false;
    right_reverse = false;
    return false;
}

MotorController::~MotorController() {
    running = false;
    cancelAutoStopTimer();
    enableEncoders(false);  // 停止编码器
    enableSafety(false);    // 停止安全监控
    if (i2c_fd >= 0) {
        close(i2c_fd);
    }
}

bool MotorController::init() {
    // 打开I2C设备
    i2c_fd = open(device_path.c_str(), O_RDWR);
    if (i2c_fd < 0) {
        std::cerr << "Failed to open I2C device: " << device_path << std::endl;
        return false;
    }

    // 设置I2C从设备地址
    if (ioctl(i2c_fd, I2C_SLAVE, pca9685_addr) < 0) {
        std::cerr << "Failed to set I2C slave address" << std::endl;
        close(i2c_fd);
        return false;
    }

    // 初始化 PCA9685
    // 
    // 这里必须由 MotorController 自己初始化 PCA9685。
    // 原因：在 libs/drive 的实现里，为了“复用 ServoMotor 初始化的 PCA9685”，
    // 我们跳过了 pca9685Init()，但实际验证会导致 EncoderReader 在申请 GPIO24/27 时出现 EBUSY。
    // motor_control_cpp 版本一直保留 pca9685Init()，编码器工作正常。
    // 
    // 结论：MotorController 不应该依赖 ServoMotor 的初始化副作用。
    if (!pca9685Init()) {
        std::cerr << "PCA9685初始化失败" << std::endl;
        return false;
    }

    // ⚠️ 注意：不要在 init() 里强行 enableEncoders(true)
    // 调用方（DriveService/测试程序）会根据需要显式启用编码器。
    if (encoders_enabled) {
        if (!initEncoders()) {
            std::cerr << "⚠️ 编码器初始化失败，但将继续运行 (编码器功能将被禁用)" << std::endl;
            encoders_enabled = false;
        }
    }

    // 尝试初始化安全监控器（可选）
    if (current_monitoring_enabled && !initSafetyMonitor()) {
        std::cerr << "⚠️ 安全监控器初始化失败，但将继续运行 (安全监控将被禁用)" << std::endl;
    }

    std::cout << "✅ 电机控制器初始化成功" << std::endl;
    return true;
}

bool MotorController::pca9685Init() {
    // MODE1: enable auto-increment, normal mode
    uint8_t mode1_data[2] = {0x00, 0x20};
    if (write(i2c_fd, mode1_data, 2) != 2) {
        std::cerr << "PCA9685: Failed to set MODE1" << std::endl;
        return false;
    }
    usleep(1000);

    // MODE2: totem pole
    uint8_t mode2_data[2] = {0x01, 0x04};
    if (write(i2c_fd, mode2_data, 2) != 2) {
        std::cerr << "PCA9685: Failed to set MODE2" << std::endl;
        return false;
    }

    // 设置PWM频率，保持默认50Hz，不用设置
    // setPWMFrequency(PWM_FREQUENCY);

    return true;
}

void MotorController::setPWMFrequency(int freq) {
    int prescale = static_cast<int>(25000000.0 / (4096.0 * freq) - 1);

    // 进入sleep mode
    uint8_t old_mode;
    uint8_t reg_data[2] = {0x00, 0x00};
    if (read(i2c_fd, &old_mode, 1) != 1) return;

    uint8_t sleep_mode = (old_mode & 0x7F) | 0x10;
    reg_data[0] = 0x00;
    reg_data[1] = sleep_mode;
    write(i2c_fd, reg_data, 2);

    // 设置prescale
    reg_data[0] = 0xFE;
    reg_data[1] = static_cast<uint8_t>(prescale);
    write(i2c_fd, reg_data, 2);

    // 退出sleep mode
    reg_data[0] = 0x00;
    reg_data[1] = old_mode;
    write(i2c_fd, reg_data, 2);
    usleep(1000);

    reg_data[0] = 0x00;
    reg_data[1] = old_mode | 0x80;
    write(i2c_fd, reg_data, 2);
}

void MotorController::setPWM(int channel, int on, int off) {
    uint8_t reg = 0x06 + channel * 4;
    uint8_t data[5] = {
        reg,
        static_cast<uint8_t>(on & 0xFF),
        static_cast<uint8_t>(on >> 8),
        static_cast<uint8_t>(off & 0xFF),
        static_cast<uint8_t>(off >> 8)
    };
    write(i2c_fd, data, 5);
}

void MotorController::setSpeeds(float left, float right, float duration) {
    std::unique_lock<std::mutex> lock(motor_mutex);

    float target_left = std::max(-1.0f, std::min(1.0f, left));
    float target_right = std::max(-1.0f, std::min(1.0f, right));

    // 简单缓启动：线性逼近目标速度（200ms，10步）
    const int RAMP_STEPS = 10;
    const int STEP_DELAY_MS = 20;  // 每步 20ms
    
    for (int i = 0; i < RAMP_STEPS; i++) {
        float progress = static_cast<float>(i + 1) / RAMP_STEPS;
        left_speed = left_speed * (1.0f - progress) + target_left * progress;
        right_speed = right_speed * (1.0f - progress) + target_right * progress;
        
        // 立即应用当前速度
        applySpeedsDirect(left_speed, right_speed);
        
        // 除了最后一步，释放锁并让其他操作穿插
        if (i < RAMP_STEPS - 1) {
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(STEP_DELAY_MS));
            lock.lock();
        }
    }

    // 确保速度精确到目标值
    left_speed = target_left;
    right_speed = target_right;

    // 如果启用了PID，使用PID控制器
    if (pid_enabled) {
        // printf("PID控制: 目标速度 L=%.2f R=%.2f\n", left_speed, right_speed);
        // 更新编码器反馈
        updateEncoderFeedbackFromHardware();

        left_pid.setSetpoint(left_speed);
        right_pid.setSetpoint(right_speed);

        // 使用编码器反馈计算PID输出
        double left_output = left_pid.compute(left_encoder_position);
        double right_output = right_pid.compute(right_encoder_position);

        applySpeedsDirect(left_output, right_output);
    } else {
        applySpeedsDirect(left_speed, right_speed);
    }

    // 处理自动停止
    if (duration > 0) {
        startAutoStopTimer(duration);
    } else if (!continuous_mode && auto_stop_timeout > 0) {
        startAutoStopTimer(auto_stop_timeout);
    }
}

void MotorController::applySpeeds() {
    int l_in1, l_in2, r_in1, r_in2;
    speedToPWM(left_speed, l_in1, l_in2, left_motor_reverse);
    speedToPWM(right_speed, r_in1, r_in2, right_motor_reverse);

    // 应用PWM补偿值
    l_in1 = static_cast<int>(l_in1 * left_pwm_compensation);
    l_in2 = static_cast<int>(l_in2 * left_pwm_compensation);
    r_in1 = static_cast<int>(r_in1 * right_pwm_compensation);
    r_in2 = static_cast<int>(r_in2 * right_pwm_compensation);

    // 确保PWM值不超过最大值
    l_in1 = std::min(l_in1, PWM_RESOLUTION - 1);
    l_in2 = std::min(l_in2, PWM_RESOLUTION - 1);
    r_in1 = std::min(r_in1, PWM_RESOLUTION - 1);
    r_in2 = std::min(r_in2, PWM_RESOLUTION - 1);

    setPWM(LEFT_IN1_CHANNEL, 0, l_in1);
    setPWM(LEFT_IN2_CHANNEL, 0, l_in2);
    setPWM(RIGHT_IN1_CHANNEL, 0, r_in1);
    setPWM(RIGHT_IN2_CHANNEL, 0, r_in2);
}

void MotorController::speedToPWM(float speed, int& in1, int& in2) {
    if (speed > 0) {
        // 修正电机接线：交换IN1和IN2信号
        in1 = 0;
        in2 = static_cast<int>(PWM_RESOLUTION * speed);
    } else if (speed < 0) {
        // 修正电机接线：交换IN1和IN2信号
        in1 = static_cast<int>(PWM_RESOLUTION * (-speed));
        in2 = 0;
    } else {
        in1 = 0;
        in2 = 0;
    }
}

void MotorController::speedToPWM(float speed, int& in1, int& in2, bool reverse) {
    // 如果需要反转方向，取反速度
    float actual_speed = reverse ? -speed : speed;

    if (actual_speed > 0) {
        // 修正电机接线：交换IN1和IN2信号
        in1 = 0;
        in2 = static_cast<int>(PWM_RESOLUTION * actual_speed);
    } else if (actual_speed < 0) {
        // 修正电机接线：交换IN1和IN2信号
        in1 = static_cast<int>(PWM_RESOLUTION * (-actual_speed));
        in2 = 0;
    } else {
        in1 = 0;
        in2 = 0;
    }
}

void MotorController::startAutoStopTimer(float duration) {
    // 取消之前的定时器
    cancelAutoStopTimer();
    
    // 重置取消标志
    auto_stop_cancel = false;
    
    // 给予足够的时间让旧线程检查取消标志并退出（旧线程每 50ms 检查一次）
    // 等待最多 100ms 确保旧线程已经看到取消信号
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    
    // 启动新定时器
    auto_stop_thread = std::thread(&MotorController::autoStopWorker, this, duration);
    auto_stop_thread.detach();
}

void MotorController::cancelAutoStopTimer() {
    // 设置取消标志，通知 autoStopWorker 提前退出
    auto_stop_cancel = true;
    
    // 等待线程检查标志并自然结束（通过 detach 后无法 join，但标志位可以让它提前退出）
    // 旧逻辑在这里只等 10ms，太短了！线程可能还没来得及检查标志
    // 改为等待 150ms，因为线程每 50ms 检查一次，这样最多等一个周期加缓冲
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
}

void MotorController::autoStopWorker(float duration) {
    // 分段睡眠，定期检查取消标志
    auto start = std::chrono::steady_clock::now();
    auto target_duration = std::chrono::milliseconds(static_cast<int>(duration * 1000));
    
    while (running && !auto_stop_cancel) {
        auto elapsed = std::chrono::steady_clock::now() - start;
        if (elapsed >= target_duration) {
            break;
        }
        
        // 每 50ms 检查一次取消标志
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // 如果被取消，直接退出（不停止电机）
    if (auto_stop_cancel) {
        return;
    }
    
    // 正常超时，停止电机
    if (running) {
        std::lock_guard<std::mutex> lock(motor_mutex);
        left_speed = 0.0f;
        right_speed = 0.0f;
        applySpeeds();
    }
}

void MotorController::stop() {
    cancelAutoStopTimer();
    setSpeeds(0.0f, 0.0f, 0.0f);
}

void MotorController::brake() {
    // 紧急刹车：立即停止，跳过缓停逻辑
    cancelAutoStopTimer();
    std::lock_guard<std::mutex> lock(motor_mutex);
    
    // 立即设置速度为0并应用
    left_speed = 0.0f;
    right_speed = 0.0f;
    applySpeedsDirect(0.0, 0.0);
}

void MotorController::forward(float speed, float duration) {
    setSpeeds(speed, speed, duration);
}

void MotorController::backward(float speed, float duration) {
    setSpeeds(-speed, -speed, duration);
}

void MotorController::turnLeft(float speed, float duration) {
    setSpeeds(-speed, speed, duration);
}

void MotorController::turnRight(float speed, float duration) {
    setSpeeds(speed, -speed, duration);
}

MotorController::MovePulsesResult MotorController::movePulses(
    long target_pulses, float throttle, double assume_rate, double timeout_multiplier) {
    
    MovePulsesResult result = {false, 0, 0, 0.0};
    
    // 检查编码器是否就绪
    if (!encoders_enabled) {
        // 如果未启用，尝试自动启用
        if (!initEncoders()) {
            std::cerr << "[movePulses] 编码器未启用或未初始化" << std::endl;
            return result;
        }
        encoders_enabled = true;
    }
    if (!left_encoder || !right_encoder) {
        std::cerr << "[movePulses] 编码器对象为空" << std::endl;
        return result;
    }
    
    if (assume_rate <= 0.0) {
        std::cerr << "[movePulses] 无效的假定速率: " << assume_rate << std::endl;
        return result;
    }
    
    // 计算方向和估算时长
    int sign = (target_pulses >= 0) ? 1 : -1;
    double needed_time = std::abs((double)target_pulses) / assume_rate;
    
    // 获取初始编码器位置
    long init_left = getLeftEncoderPosition();
    long init_right = getRightEncoderPosition();
    printf("[movePulses] 目标脉冲: %ld, 初始位置: 左=%ld 右=%ld, 估计时间: %.2f 秒\n",
           target_pulses, init_left, init_right, needed_time);
    // 启动电机
    float left_speed = throttle * sign;
    float right_speed = throttle * sign;
    
    // 修正：使用更长的超时时间作为自动停止时间，而不是预估时间
    // 这样允许闭环控制逻辑在目标达成时主动停止，而不是被自动停止打断造成未达标
    double timeout = needed_time * timeout_multiplier + 1.0f;
    setSpeeds(left_speed, right_speed, static_cast<float>(timeout));
    
    // 等待初始加速
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 开始跟踪
    auto start = std::chrono::steady_clock::now();
    
    while (running) {
        long left_pos = getLeftEncoderPosition();
        long right_pos = getRightEncoderPosition();
        long delta_left = left_pos - init_left;
        long delta_right = right_pos - init_right;
        
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        
        // 检查是否到达目标
        if (std::abs(delta_left) >= std::abs(target_pulses) && 
            std::abs(delta_right) >= std::abs(target_pulses)) {
            result.reached = true;
            result.left_pulses = delta_left;
            result.right_pulses = delta_right;
            result.elapsed_time = elapsed;
            break;
        }
        
        // 超时保护
        if (elapsed > timeout) {
            result.reached = false;
            result.left_pulses = delta_left;
            result.right_pulses = delta_right;
            result.elapsed_time = elapsed;
            break;
        }
        
        // 检查取消标志
        if (auto_stop_cancel) {
            result.reached = false;
            result.left_pulses = delta_left;
            result.right_pulses = delta_right;
            result.elapsed_time = elapsed;
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // 停止电机
    stop();
    
    return result;
}

MotorController::MovePulsesResult MotorController::turnPulses(
    long target_pulses, float throttle, bool turn_left, double assume_rate, double timeout_multiplier) {
    
    MovePulsesResult result = {false, 0, 0, 0.0};
    
    // 检查编码器是否就绪
    if (!encoders_enabled) {
        // 如果未启用，尝试自动启用
        if (!initEncoders()) {
             std::cerr << "[turnPulses] 编码器未启用或未初始化" << std::endl;
             return result;
        }
        encoders_enabled = true;
    }
    if (!left_encoder || !right_encoder) {
        std::cerr << "[turnPulses] 编码器对象为空" << std::endl;
        return result;
    }
    
    if (assume_rate <= 0.0) {
        std::cerr << "[turnPulses] 无效的假定速率: " << assume_rate << std::endl;
        return result;
    }
    
    long abs_target = std::abs(target_pulses);
    double needed_time = (double)abs_target / assume_rate;
    
    // 获取初始编码器位置
    long init_left = getLeftEncoderPosition();
    long init_right = getRightEncoderPosition();
    printf("[turnPulses] 目标脉冲: %ld, 方向: %s, 估计时间: %.2f 秒\n",
           abs_target, turn_left ? "左转" : "右转", needed_time);
           
    // 设定速度：原地旋转
    // 左转：左轮后退(-)，右轮前进(+)
    // 右转：左轮前进(+)，右轮后退(-)
    float left_speed = turn_left ? -throttle : throttle;
    float right_speed = turn_left ? throttle : -throttle;
    
    // 修正：使用更长的超时时间作为自动停止时间
    double timeout = needed_time * timeout_multiplier + 1.0f;
    setSpeeds(left_speed, right_speed, static_cast<float>(timeout));
    
    // 等待初始加速
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    // 开始跟踪
    auto start = std::chrono::steady_clock::now();
    
    while (running) {
        long left_pos = getLeftEncoderPosition();
        long right_pos = getRightEncoderPosition();
        long delta_left = left_pos - init_left;
        long delta_right = right_pos - init_right;
        
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - start).count();
        
        // 检查是否到达目标 (两个轮子都必须转过足够角度)
        if (std::abs(delta_left) >= abs_target && 
            std::abs(delta_right) >= abs_target) {
            result.reached = true;
            result.left_pulses = delta_left;
            result.right_pulses = delta_right;
            result.elapsed_time = elapsed;
            break;
        }
        
        // 超时保护
        if (elapsed > timeout) {
            result.reached = false;
            result.left_pulses = delta_left;
            result.right_pulses = delta_right;
            result.elapsed_time = elapsed;
            break;
        }
        
        // 检查取消标志
        if (auto_stop_cancel) {
            result.reached = false;
            result.left_pulses = delta_left;
            result.right_pulses = delta_right;
            result.elapsed_time = elapsed;
            break;
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    
    // 停止电机
    stop();
    
    return result;
}

void MotorController::setContinuousMode(bool enabled) {
    continuous_mode = enabled;
    if (enabled) {
        cancelAutoStopTimer();
    }
}

void MotorController::setAutoStopTimeout(float timeout) {
    auto_stop_timeout = timeout;
}

void MotorController::enablePID(bool enabled) {
    pid_enabled = enabled;
    if (enabled) {
        left_pid.reset();
        right_pid.reset();
    }
}

void MotorController::setPIDParameters(double kp, double ki, double kd) {
    left_pid.setPID(kp, ki, kd);
    right_pid.setPID(kp, ki, kd);
    left_pid.setLimits(-1.0, 1.0);
    right_pid.setLimits(-1.0, 1.0);
}

void MotorController::updateEncoderFeedback(double left_pos, double right_pos) {
    left_encoder_position = left_pos;
    right_encoder_position = right_pos;
}

void MotorController::applySpeedsDirect(double left, double right) {
    int l_in1, l_in2, r_in1, r_in2;
    // 应用平衡补偿系数（编码器误差补偿）
    // 实际速度 = 设定速度 * 平衡系数
    speedToPWM(static_cast<float>(left * left_scaler), l_in1, l_in2, left_motor_reverse);
    speedToPWM(static_cast<float>(right * right_scaler), r_in1, r_in2, right_motor_reverse);

    // 应用PWM补偿值
    l_in1 = static_cast<int>(l_in1 * left_pwm_compensation);
    l_in2 = static_cast<int>(l_in2 * left_pwm_compensation);
    r_in1 = static_cast<int>(r_in1 * right_pwm_compensation);
    r_in2 = static_cast<int>(r_in2 * right_pwm_compensation);

    // 确保PWM值不超过最大值
    l_in1 = std::min(l_in1, PWM_RESOLUTION - 1);
    l_in2 = std::min(l_in2, PWM_RESOLUTION - 1);
    r_in1 = std::min(r_in1, PWM_RESOLUTION - 1);
    r_in2 = std::min(r_in2, PWM_RESOLUTION - 1);

    setPWM(LEFT_IN1_CHANNEL, 0, l_in1);
    setPWM(LEFT_IN2_CHANNEL, 0, l_in2);
    setPWM(RIGHT_IN1_CHANNEL, 0, r_in1);
    setPWM(RIGHT_IN2_CHANNEL, 0, r_in2);
}

void MotorController::enableEncoders(bool enabled) {
    if (enabled && !encoders_enabled) {
        // 启用编码器
        if (initEncoders()) {
            encoders_enabled = true;
            std::cout << "编码器已启用" << std::endl;
        }
    } else if (!enabled && encoders_enabled) {
        // 禁用编码器
        if (left_encoder) {
            left_encoder->stop();
            delete left_encoder;
            left_encoder = nullptr;
        }
        if (right_encoder) {
            right_encoder->stop();
            delete right_encoder;
            right_encoder = nullptr;
        }
        encoders_enabled = false;
        std::cout << "编码器已禁用" << std::endl;
    }
}

void MotorController::setEncoderDebugEnabled(bool enabled) {
    if (left_encoder) {
        left_encoder->setDebugEnabled(enabled);
    }
    if (right_encoder) {
        right_encoder->setDebugEnabled(enabled);
    }
}

bool MotorController::initEncoders() {
    
    if (!encoder_config_.enable_encoder) {
        std::cout << "\n[initEncoders] ⚠️ 编码器已禁用，跳过编码器初始化！" << std::endl;
        return true;
    }
    try {
        // 根据硬件文档配置编码器引脚
        // 左电机编码器: GPIO13 (A), GPIO6 (B)
        // 右电机编码器: GPIO27 (A), GPIO0 (B)

        left_encoder = new EncoderReader(6, 24, "left_encoder");
        right_encoder = new EncoderReader(0, 27, "right_encoder");

        bool any_ok = false;

        bool left_ok = false;
        try {
            left_ok = left_encoder && left_encoder->init();
        } catch (...) { left_ok = false; }
        if (!left_ok) {
            if (encoders_enabled) std::cerr << "左编码器初始化失败，左通道将被禁用" << std::endl;
            if (left_encoder) { delete left_encoder; left_encoder = nullptr; }
        } else {
            left_encoder->start();
            any_ok = true;
        }

        bool right_ok = false;
        try {
            right_ok = right_encoder && right_encoder->init();
        } catch (...) { right_ok = false; }
        if (!right_ok) {
            if (encoders_enabled) std::cerr << "右编码器初始化失败，右通道将被禁用" << std::endl;
            if (right_encoder) { delete right_encoder; right_encoder = nullptr; }
        } else {
            right_encoder->start();
            any_ok = true;
        }

        if (!any_ok) {
            if (encoders_enabled) std::cerr << "编码器初始化失败: 未能初始化任一通道" << std::endl;
            return false;
        }

        return true;
    } catch (const std::exception& e) {
        std::cerr << "编码器初始化异常: " << e.what() << std::endl;
        return false;
    }
}

void MotorController::updateEncoderFeedbackFromHardware() {
    if (!encoders_enabled || !left_encoder || !right_encoder) {
        return;
    }

    // 从硬件读取编码器位置
    double left_pos = left_encoder->getPosition();
    double right_pos = right_encoder->getPosition();

    // 更新PID控制器
    updateEncoderFeedback(left_pos, right_pos);
}

void MotorController::enableSafety(bool enabled) {
    if (enabled && !safety_enabled) {
        // 启用安全监控
        if (initSafetyMonitor()) {
            safety_enabled = true;
            std::cout << "安全监控已启用" << std::endl;
        }
    } else if (!enabled && safety_enabled) {
        // 禁用安全监控
        if (safety_monitor) {
            safety_monitor->stop();
            delete safety_monitor;
            safety_monitor = nullptr;
        }
        safety_enabled = false;
        std::cout << "安全监控已禁用" << std::endl;
    }
}

bool MotorController::initSafetyMonitor() {
    if (!current_monitoring_enabled) {
        std::cout << "电流监测未启用，跳过安全监控器初始化" << std::endl;
        return true;  // 不启用时认为成功
    }

    try {
        safety_monitor = new SafetyMonitor();

        if (!safety_monitor->init()) {
            std::cerr << "安全监控器初始化失败" << std::endl;
            return false;
        }

        // 设置安全阈值
        safety_monitor->setCurrentLimit(1.5);      // 1.5A
        safety_monitor->setVoltageLimits(3.0, 5.2); // 3.0-5.2V
        safety_monitor->setTemperatureLimit(65.0);  // 65°C

        // 设置安全回调
        safety_monitor->setSafetyCallback(
            [this](const std::string& message) {
                this->safetyCallback(message);
            }
        );

        safety_monitor->start();
        return true;

    } catch (const std::exception& e) {
        std::cerr << "安全监控器初始化异常: " << e.what() << std::endl;
        return false;
    }
}

void MotorController::safetyCallback(const std::string& message) {
    std::cerr << "🔴 安全警报: " << message << std::endl;

    // 安全事件处理：立即停止电机
    stop();

    // 这里可以添加更多的安全响应逻辑
    // 比如发送通知、记录日志等
}

bool MotorController::isSafe() const {
    if (!current_monitoring_enabled) {
        return true;  // 如果未启用电流监测，认为安全
    }
    if (!safety_enabled || !safety_monitor) {
        return true;  // 如果未启用安全监控，认为安全
    }
    return safety_monitor->isSafe();
}

bool MotorController::hasLeftEncoder() const {
    return encoders_enabled && left_encoder != nullptr;
}

bool MotorController::hasRightEncoder() const {
    return encoders_enabled && right_encoder != nullptr;
}

// 获取编码器数据
long MotorController::getLeftEncoderPosition() const {
    if (left_encoder && encoders_enabled) {
        std::lock_guard<std::mutex> lock(encoder_offset_mutex);
        // 返回相对位置 = 当前绝对值 - 偏移量
        return left_encoder->getPosition() - left_encoder_offset;
    }
    return 0;
}

long MotorController::getRightEncoderPosition() const {
    if (right_encoder && encoders_enabled) {
        std::lock_guard<std::mutex> lock(encoder_offset_mutex);
        // 返回相对位置 = 当前绝对值 - 偏移量
        return right_encoder->getPosition() - right_encoder_offset;
    }
    return 0;
}

long MotorController::getLeftEncoderDelta() {
    if (left_encoder && encoders_enabled) {
        return left_encoder->getDeltaPosition();
    }
    return 0;
}

long MotorController::getRightEncoderDelta() {
    if (right_encoder && encoders_enabled) {
        return right_encoder->getDeltaPosition();
    }
    return 0;
}

void MotorController::enableCurrentMonitoring(bool enabled) {
    if (current_monitoring_enabled == enabled) {
        return;  // 状态未改变
    }

    current_monitoring_enabled = enabled;

    if (enabled) {
        std::cout << "电流监测已启用" << std::endl;
        // 如果电机控制器已经初始化，尝试初始化安全监控器
        if (i2c_fd >= 0 && !safety_monitor) {
            if (!initSafetyMonitor()) {
                std::cerr << "启用电流监测时初始化安全监控器失败" << std::endl;
            }
        }
    } else {
        std::cout << "电流监测已禁用" << std::endl;
        // 禁用时停止安全监控器
        enableSafety(false);
    }
}

// ============================================================
// ===== M2 新增 API：精确距离与角度控制 =====
// ============================================================

bool MotorController::load_encoder_config(const std::string& config_file) {
    // 初始化默认值
    encoder_config_.pulses_per_revolution = 12;
    encoder_config_.wheel_diameter_cm = 3.0f;
    encoder_config_.wheel_circumference_cm = 9.4248f;
    encoder_config_.cm_per_pulse = 0.7854f;
    encoder_config_.axle_width_cm = 10.5f;
    encoder_config_.turn_compensation_factor = 1.0f;
    encoder_config_.position_tolerance = 2;
    encoder_config_.angle_tolerance_deg = 2.0f;
    encoder_config_.enable_pid = false;           // 默认关闭 PID
    encoder_config_.enable_encoder = false;           // 默认关闭 PID
    encoder_config_.debug_encoder = true;         // 默认启用编码器调试
    encoder_config_.debug_pulses = true;          // 默认启用脉冲调试

    // 尝试从配置文件读取
    std::ifstream ifs(config_file);
    if (!ifs.is_open()) {
        std::cout << "[MotorController] 编码器配置：使用默认值（文件未找到）" << std::endl;
        return true;  // 使用默认值
    }

    std::string line;
    bool in_encoder_section = false;
    bool in_balance_section = false;
    bool in_pid_control_section = false;
    bool in_pid_position_section = false;

    while (std::getline(ifs, line)) {
        // 移除注释和空格
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        // 去除首尾空格
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) continue;

        // 检查 [encoder] 段
        if (line == "[encoder]") {
            in_encoder_section = true;
            in_balance_section = false;
            in_pid_control_section = false;
            in_pid_position_section = false;
            continue;
        }
        
        // 检查 [balance] 段
        if (line == "[balance]") {
            in_balance_section = true;
            in_encoder_section = false;
            in_pid_control_section = false;
            in_pid_position_section = false;
            continue;
        }

        // 检查 [pid_control] 段 (PID 总开关)
        if (line == "[pid_control]") {
            in_pid_control_section = true;
            in_encoder_section = false;
            in_balance_section = false;
            in_pid_position_section = false;
            continue;
        }
        
        // 检查 [pid_position] 段 (PID 参数)
        if (line == "[pid_position]") {
            in_pid_position_section = true;
            in_encoder_section = false;
            in_balance_section = false;
            in_pid_control_section = false;
            continue;
        }
        
        // 检查其他段
        if (line[0] == '[') {
            in_encoder_section = false;
            in_balance_section = false;
            in_pid_control_section = false;
            in_pid_position_section = false;
            continue;
        }

        if (!in_encoder_section && !in_balance_section && !in_pid_control_section && !in_pid_position_section) continue;

        // 解析 key=value 对
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(0, eq_pos);
        std::string value_str = line.substr(eq_pos + 1);

        // 去除 key/value 的空格
        key.erase(key.find_last_not_of(" \t") + 1);
        value_str.erase(0, value_str.find_first_not_of(" \t"));
        value_str.erase(value_str.find_last_not_of(" \t") + 1);

        try {
            if (in_encoder_section) {
                // PID 总开关配置
                if (key == "enable_encoder") {
                    encoder_config_.enable_encoder = (value_str == "true" || value_str == "1");
                }
                if (key == "pulses_per_revolution") {
                    encoder_config_.pulses_per_revolution = std::stoi(value_str);
                } else if (key == "wheel_diameter_cm") {
                    encoder_config_.wheel_diameter_cm = std::stof(value_str);
                } else if (key == "wheel_circumference_cm") {
                    encoder_config_.wheel_circumference_cm = std::stof(value_str);
                } else if (key == "cm_per_pulse") {
                    encoder_config_.cm_per_pulse = std::stof(value_str);
                } else if (key == "axle_width_cm") {
                    encoder_config_.axle_width_cm = std::stof(value_str);
                } else if (key == "turn_compensation_factor") {
                    encoder_config_.turn_compensation_factor = std::stof(value_str);
                } else if (key == "position_tolerance") {
                    encoder_config_.position_tolerance = std::stoi(value_str);
                } else if (key == "angle_tolerance_deg") {
                    encoder_config_.angle_tolerance_deg = std::stof(value_str);
                } else if (key == "debug_encoder") {
                    encoder_config_.debug_encoder = (value_str == "true" || value_str == "1");
                } else if (key == "debug_pulses") {
                    encoder_config_.debug_pulses = (value_str == "true" || value_str == "1");
                }
            } else if (in_balance_section) {
                // 平衡系数配置
                if (key == "left_scaler") {
                    left_scaler = std::stof(value_str);
                } else if (key == "right_scaler") {
                    right_scaler = std::stof(value_str);
                }
            } else if (in_pid_control_section) {
                // PID 总开关配置
                if (key == "enable_pid") {
                    encoder_config_.enable_pid = (value_str == "true" || value_str == "1");
                }
            } else if (in_pid_position_section) {
                // PID 位置控制配置 (M2 新增)
                if (key == "position_kp") {
                    position_pid_config_.kp = std::stof(value_str);
                } else if (key == "position_ki") {
                    position_pid_config_.ki = std::stof(value_str);
                } else if (key == "position_kd") {
                    position_pid_config_.kd = std::stof(value_str);
                } else if (key == "position_ki_limit") {
                    position_pid_config_.ki_limit = std::stof(value_str);
                } else if (key == "position_output_min") {
                    position_pid_config_.output_min = std::stof(value_str);
                } else if (key == "position_output_max") {
                    position_pid_config_.output_max = std::stof(value_str);
                } else if (key == "position_control_period_ms") {
                    position_pid_config_.control_period_ms = std::stoi(value_str);
                } else if (key == "pwm_deadzone") {
                    encoder_config_.pwm_deadzone = std::stof(value_str);
                } else if (key == "pwm_min_active") {
                    encoder_config_.pwm_min_active = std::stof(value_str);
                } else if (key == "pwm_max") {
                    encoder_config_.pwm_max = std::stof(value_str);
                }
            }
        } catch (const std::exception& e) {
            std::cerr << "[MotorController] 配置解析错误: " << key << "=" << value_str << std::endl;
        }
    }

    ifs.close();

    std::cout << "[MotorController] 编码器配置已加载:" << std::endl;
    std::cout << "  启用编码器: " << (encoder_config_.enable_encoder ? "是" : "否") << std::endl;
    std::cout << "  PPR: " << encoder_config_.pulses_per_revolution << std::endl;
    std::cout << "  轮径: " << encoder_config_.wheel_diameter_cm << " cm" << std::endl;
    std::cout << "  轮周: " << encoder_config_.wheel_circumference_cm << " cm" << std::endl;
    std::cout << "  轮距: " << encoder_config_.axle_width_cm << " cm" << std::endl;
    std::cout << "  每脉冲距离: " << encoder_config_.cm_per_pulse << " cm" << std::endl;
    std::cout << "  启用PID: " << (encoder_config_.enable_pid ? "是" : "否") << std::endl;
    std::cout << "  平衡系数: L=" << left_scaler << ", R=" << right_scaler << std::endl;
    
    // 输出 PID 配置
    std::cout << "[MotorController] PID 位置控制参数已加载:" << std::endl;
    std::cout << "  Kp: " << position_pid_config_.kp << std::endl;
    std::cout << "  Ki: " << position_pid_config_.ki << std::endl;
    std::cout << "  Kd: " << position_pid_config_.kd << std::endl;
    std::cout << "  Ki 限制: " << position_pid_config_.ki_limit << std::endl;
    std::cout << "  输出范围: [" << position_pid_config_.output_min << "%, " 
              << position_pid_config_.output_max << "%]" << std::endl;
    std::cout << "  控制周期: " << position_pid_config_.control_period_ms << " ms" << std::endl;
    
    // 输出 PWM 死区参数
    std::cout << "[MotorController] PWM 死区参数:" << std::endl;
    std::cout << "  PWM 死区: " << encoder_config_.pwm_deadzone << std::endl;
    std::cout << "  最小有效 PWM: " << encoder_config_.pwm_min_active << std::endl;
    std::cout << "  最大 PWM: " << encoder_config_.pwm_max << std::endl;

    return true;
}

void MotorController::load_pwm_compensation(const std::string& config_file) {
    std::ifstream ifs(config_file);
    if (!ifs.is_open()) {
        std::cout << "[MotorController] PWM补偿：使用默认值（文件未找到）" << std::endl;
        return;
    }

    std::string line;
    bool in_motor_section = false;

    while (std::getline(ifs, line)) {
        // 移除注释和空格
        size_t comment_pos = line.find('#');
        if (comment_pos != std::string::npos) {
            line = line.substr(0, comment_pos);
        }
        
        // 去除首尾空格
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty()) continue;

        // 检查 [motor] 段
        if (line == "[motor]") {
            in_motor_section = true;
            continue;
        }

        // 检查其他段
        if (line[0] == '[') {
            in_motor_section = false;
            continue;
        }

        if (!in_motor_section) continue;

        // 解析 key=value 对
        size_t eq_pos = line.find('=');
        if (eq_pos == std::string::npos) continue;

        std::string key = line.substr(0, eq_pos);
        std::string value_str = line.substr(eq_pos + 1);

        // 去除 key/value 的空格
        key.erase(key.find_last_not_of(" \t") + 1);
        value_str.erase(0, value_str.find_first_not_of(" \t"));
        value_str.erase(value_str.find_last_not_of(" \t") + 1);

        try {
            if (key == "left_pwm_compensation") {
                left_pwm_compensation = std::stof(value_str);
                std::cout << "⚙️ 左电机PWM补偿值已设置为 " << left_pwm_compensation << std::endl;
            } else if (key == "right_pwm_compensation") {
                right_pwm_compensation = std::stof(value_str);
                std::cout << "⚙️ 右电机PWM补偿值已设置为 " << right_pwm_compensation << std::endl;
            }
        } catch (...) {
            // ignore parse errors
        }
    }

    ifs.close();
}

int32_t MotorController::distance_to_pulses(float distance_cm) const {
    return static_cast<int32_t>(std::round(distance_cm / encoder_config_.cm_per_pulse));
}

float MotorController::pulses_to_distance(int32_t pulses) const {
    return pulses * encoder_config_.cm_per_pulse;
}

int32_t MotorController::angle_to_pulse_diff(float angle_deg) const {
    // 原地转向：角度 = (脉冲差 × 轮周长) / 轮距
    // 脉冲差 = (角度 × 轮距 × π/180) / (π × 轮径 / PPR)
    const float radian = angle_deg * M_PI / 180.0f;
    const float pulse_diff = (radian * encoder_config_.axle_width_cm) / 
                             (M_PI * encoder_config_.wheel_diameter_cm / encoder_config_.pulses_per_revolution);
    return static_cast<int32_t>(std::round(pulse_diff * encoder_config_.turn_compensation_factor));
}

bool MotorController::move_distance_cm(float distance_cm, float throttle, uint32_t timeout_ms) {
    if (!encoders_enabled) {
        // 如果未启用，尝试自动启用
        if (!initEncoders()) {
            std::cerr << "[move_distance_cm] 编码器未启用或未初始化" << std::endl;
            return false;
        }
        encoders_enabled = true;
    }
    if (!left_encoder || !right_encoder) {
        std::cerr << "[move_distance_cm] 编码器对象为空" << std::endl;
        return false;
    }

    int32_t target_pulses = distance_to_pulses(distance_cm);
    
    std::cout << "[move_distance_cm] ====== 运动开始 ======" << std::endl;
    std::cout << "[move_distance_cm] 目标距离: " << distance_cm << " cm" << std::endl;
    std::cout << "[move_distance_cm] 脉冲计算: distance_cm / cm_per_pulse = " << distance_cm 
              << " / " << encoder_config_.cm_per_pulse << " = " << target_pulses << std::endl;
    std::cout << "[move_distance_cm] 配置参数: PPR=" << encoder_config_.pulses_per_revolution 
              << ", 轮径=" << encoder_config_.wheel_diameter_cm 
              << " cm, 轮周=" << encoder_config_.wheel_circumference_cm 
              << " cm, 轮距=" << encoder_config_.axle_width_cm << " cm" << std::endl;
    std::cout << "[move_distance_cm] throttle: " << throttle << ", timeout: " << timeout_ms << "ms" << std::endl;

    // 重置编码器
    int32_t left_before = getLeftEncoderPosition();
    int32_t right_before = getRightEncoderPosition();
    std::cout << "[move_distance_cm] 重置前编码器值: L=" << left_before << ", R=" << right_before << std::endl;
    
    reset_encoders();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    int32_t left_after = getLeftEncoderPosition();
    int32_t right_after = getRightEncoderPosition();
    std::cout << "[move_distance_cm] 重置后编码器值: L=" << left_after << ", R=" << right_after << std::endl;

    // 启动电机
    if (target_pulses > 0) {
        forward(throttle);
    } else {
        backward(std::abs(throttle));
    }

    // 等待完成
    auto start_time = std::chrono::high_resolution_clock::now();
    int32_t tolerance = encoder_config_.position_tolerance;
    // 简单的超调保护，防止失控。阈值设为目标脉冲的2倍。
    int32_t overshoot_threshold = std::abs(target_pulses) * 2;
    
    // 提前停止距离（脉冲）：在接近目标时提前停止，补偿惯性
    // 根据速度调整：速度越高，提前量越大
    int32_t early_stop_distance = std::max(10, static_cast<int32_t>(std::abs(target_pulses) * 0.15));
    if (throttle > 0.25) {
        early_stop_distance = static_cast<int32_t>(early_stop_distance * 1.5);
    }

    while (true) {
        auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
        uint32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        if (elapsed_ms > timeout_ms) {
            std::cerr << "[move_distance_cm] 超时（" << elapsed_ms << "ms > " << timeout_ms << "ms）" << std::endl;
            stop();
            return false;
        }

        int32_t left_pos = getLeftEncoderPosition();
        int32_t right_pos = getRightEncoderPosition();
        int32_t avg_pos = (left_pos + right_pos) / 2;

        // 简单的超调保护
        if (std::abs(avg_pos) > overshoot_threshold && elapsed_ms > 200) {
            std::cerr << "[move_distance_cm] ❌ 触发基本超调保护: avg=" << avg_pos 
                      << ", threshold=" << overshoot_threshold << std::endl;
            stop();
            return false;
        }

        if (encoder_config_.debug_pulses && elapsed_ms % 200 < 50) {
            std::cout << "[move_distance_cm] " << std::setw(4) << elapsed_ms << "ms: "
                      << "L=" << std::setw(3) << left_pos << " R=" << std::setw(3) << right_pos 
                      << " avg=" << std::setw(3) << avg_pos << "/" << target_pulses 
                      << " diff=" << (avg_pos - target_pulses) << " early_stop=" << early_stop_distance << std::endl;
        }

        // 提前停止：在接近目标时就停止电机，利用惯性滑行到目标
        int32_t distance_to_target = std::abs(avg_pos - target_pulses);
        if (distance_to_target <= early_stop_distance) {
            stop();
            // 等待惯性停止
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            
            // 读取最终位置
            int32_t final_left = getLeftEncoderPosition();
            int32_t final_right = getRightEncoderPosition();
            int32_t final_avg = (final_left + final_right) / 2;
            int32_t final_error = final_avg - target_pulses;
            
            if (std::abs(final_error) <= tolerance) {
                std::cout << "[move_distance_cm] ✅ SUCCESS: 平均位置=" << final_avg 
                          << ", 目标=" << target_pulses << ", 误差=" << final_error << std::endl;
                return true;
            } else {
                std::cout << "[move_distance_cm] ⚠️  提前停止后误差较大: 平均位置=" << final_avg 
                          << ", 目标=" << target_pulses << ", 误差=" << final_error 
                          << " (容限=" << tolerance << ")" << std::endl;
                // 误差在可接受范围内（容限的2倍）也算成功
                if (std::abs(final_error) <= tolerance * 3) {
                    return true;
                }
                return false;
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool MotorController::turn_deg(float angle_deg, float throttle, uint32_t timeout_ms) {
    if (!encoders_enabled) {
        // 如果未启用，尝试自动启用
        if (!initEncoders()) {
            std::cerr << "[turn_deg] 编码器未启用或未初始化" << std::endl;
            return false;
        }
        encoders_enabled = true;
    }
    if (!left_encoder || !right_encoder) {
        std::cerr << "[turn_deg] 编码器对象为空" << std::endl;
        return false;
    }

    int32_t pulse_diff = angle_to_pulse_diff(angle_deg);
    bool turn_left = (angle_deg < 0);  // 逆时针为负

    std::cout << "[turn_deg] 目标角度: " << angle_deg << "°, "
              << "脉冲差: " << pulse_diff << ", 方向: " << (turn_left ? "左" : "右") << std::endl;

    // 重置编码器
    reset_encoders();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    // 启动转向
    if (turn_left) {
        turnLeft(throttle);
    } else {
        turnRight(throttle);
    }

    // 等待完成
    auto start_time = std::chrono::high_resolution_clock::now();
    int32_t tolerance = encoder_config_.position_tolerance;

    while (true) {
        auto elapsed = std::chrono::high_resolution_clock::now() - start_time;
        uint32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

        if (elapsed_ms > timeout_ms) {
            std::cerr << "[turn_deg] 超时（" << timeout_ms << "ms）" << std::endl;
            stop();
            return false;
        }

        int32_t left_pos = getLeftEncoderPosition();
        int32_t right_pos = getRightEncoderPosition();
        int32_t current_diff = std::abs(left_pos - right_pos);

        // 检查是否完成转向（使用绝对值的脉冲差）
        if (current_diff >= (std::abs(pulse_diff) - tolerance)) {
            stop();
            std::cout << "[turn_deg] SUCCESS: 实际脉冲差=" << current_diff 
                      << ", 目标=" << std::abs(pulse_diff) << std::endl;
            return true;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool MotorController::motor_stop_and_wait() {
    stop();
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    return true;
}

int32_t MotorController::get_left_encoder_value() const {
    return getLeftEncoderPosition();
}

int32_t MotorController::get_right_encoder_value() const {
    return getRightEncoderPosition();
}

void MotorController::reset_encoders() {
    // 保存当前的绝对位置作为偏移量基准，后续相对位置 = 绝对 - 偏移
    std::lock_guard<std::mutex> lock(encoder_offset_mutex);
    
    if (left_encoder) {
        left_encoder_offset = left_encoder->getPosition();
    }
    if (right_encoder) {
        right_encoder_offset = right_encoder->getPosition();
    }
}

// ============================================================
// ===== 平衡补偿 (编码器误差补偿) 实现 =====
// ============================================================

void MotorController::setBalanceScalers(float left, float right) {
    left_scaler = left;
    right_scaler = right;
}

void MotorController::setPWMCompensation(float left, float right) {
    left_pwm_compensation = left;
    right_pwm_compensation = right;
    std::cout << "⚙️ PWM补偿值已设置: 左=" << left << ", 右=" << right << std::endl;
}

// 注意: save_balance_scalers 在头文件中未声明，暂时注释掉
// } // 函数体结束


// ========================= PID 位置控制实现 (M2 新增) =========================

void MotorController::setPIDConfig(const PositionPIDConfig& config) {
    std::lock_guard<std::mutex> lock(pid_state_mutex_);
    position_pid_config_ = config;
    
    std::cout << "[PID] 配置已更新:" << std::endl;
    std::cout << "  Kp=" << config.kp << ", Ki=" << config.ki << ", Kd=" << config.kd << std::endl;
    std::cout << "  Ki_limit=" << config.ki_limit << std::endl;
    std::cout << "  Output=[" << config.output_min << "%, " << config.output_max << "%]" << std::endl;
}

float MotorController::calculatePID(int32_t error, float dt, PIDState& state, const PositionPIDConfig& config) {
    // P 项：比例响应
    float p_term = config.kp * error;
    
    // I 项：积分累积（带抗饱和）
    state.integral += error * dt;
    state.integral = std::clamp(state.integral, -config.ki_limit, config.ki_limit);
    float i_term = config.ki * state.integral;
    
    // D 项：微分阻尼
    float derivative = (error - state.last_error) / dt;
    float d_term = config.kd * derivative;
    state.last_error = error;
    
    // 总输出
    float output = p_term + i_term + d_term;
    output = std::clamp(output, config.output_min, config.output_max);
    
    if (encoder_config_.debug_pulses) {
        std::cout << "[PID] err=" << error 
                  << " P=" << std::fixed << std::setprecision(2) << p_term
                  << " I=" << i_term
                  << " D=" << d_term
                  << " out=" << output << "%" << std::endl;
    }
    
    return output;
}

// ========================= 分阶段速度控制策略（非 PID 模式） =========================
// 当 enable_pid=false 时，使用此函数
// 策略：快速加速 -> 巡航 -> 减速 -> 精细爬行
bool MotorController::move_distance_cm_with_profile(float distance_cm, float max_speed, uint32_t timeout_ms) {
    if (!encoders_enabled) {
        // 如果未启用，尝试自动启用
        if (!initEncoders()) {
            std::cerr << "[move_distance_cm_profile] ❌ 编码器未启用或未初始化" << std::endl;
            return false;
        }
        encoders_enabled = true;
    }
    if (!left_encoder || !right_encoder) {
        std::cerr << "[move_distance_cm_profile] ❌ 编码器对象为空" << std::endl;
        return false;
    }

    int32_t target_pulses = distance_to_pulses(distance_cm);
    int32_t tolerance = encoder_config_.position_tolerance;
    int32_t abs_target = std::abs(target_pulses);
    
    std::cout << "\n[move_distance_cm_profile] ====== 分阶段速度控制开始 ======" << std::endl;
    std::cout << "[move_distance_cm_profile] 目标距离: " << distance_cm << " cm" << std::endl;
    std::cout << "[move_distance_cm_profile] 目标脉冲: " << target_pulses << std::endl;
    std::cout << "[move_distance_cm_profile] 最大速度: " << max_speed * 100.0f << "%" << std::endl;
    std::cout << "[move_distance_cm_profile] 控制策略: 快速(100%) -> 巡航(" << (max_speed * 100.0f) 
              << "%) -> 减速(30%) -> 爬行(15%)" << std::endl;

    reset_encoders();
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto start_time = std::chrono::steady_clock::now();
    int32_t last_log_ms = 0;
    
    // 定义阶段距离（相对于目标距离的百分比）
    float stage1_percent = 0.30f;    // 快速加速到 30% 距离
    float stage2_percent = 0.80f;    // 巡航到 80% 距离
    // stage3: 80% - 95% 减速
    // stage4: 95% - 100% 精细爬行

    while (true) {
        auto now = std::chrono::steady_clock::now();
        uint32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

        // 超时检查
        if (elapsed_ms > timeout_ms) {
            stop();
            std::cerr << "[move_distance_cm_profile] ❌ 超时 (" << elapsed_ms << "ms > " << timeout_ms << "ms)" << std::endl;
            return false;
        }

        // 读取当前位置
        int32_t left_pos = getLeftEncoderPosition();
        int32_t right_pos = getRightEncoderPosition();
        int32_t avg_pos = (left_pos + right_pos) / 2;
        int32_t error = target_pulses - avg_pos;

        // 日志输出（100ms 一次）
        if (elapsed_ms - last_log_ms >= 100) {
            std::cout << "[move_distance_cm_profile] t=" << std::setw(4) << elapsed_ms << "ms | "
                      << "pos=" << std::setw(3) << avg_pos << "/" << target_pulses 
                      << " err=" << std::setw(3) << error 
                      << " | progress=" << std::setw(3) << (100 * avg_pos / abs_target) << "%" << std::endl;
            last_log_ms = elapsed_ms;
        }

        // 到达目标（容限内）
        if (std::abs(error) <= tolerance) {
            std::cout << "[move_distance_cm_profile] ✅ 到达目标！最终位置=" << avg_pos 
                      << " 目标=" << target_pulses << " 误差=" << error << std::endl;
            stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            return true;
        }

        // 确定当前阶段并计算速度
        float progress_percent = static_cast<float>(std::abs(avg_pos)) / abs_target;
        float throttle = max_speed;  // 默认最大速度

        if (progress_percent < stage1_percent) {
            // 阶段1: 快速加速（100%）
            throttle = 1.0f;
        } else if (progress_percent < stage2_percent) {
            // 阶段2: 巡航（max_speed）
            throttle = max_speed;
        } else if (progress_percent < 0.95f) {
            // 阶段3: 减速（30%）
            throttle = 0.30f;
        } else {
            // 阶段4: 精细爬行（15%）
            throttle = 0.15f;
        }

        // 执行移动
        if (error > 0) {
            forward(throttle);
        } else {
            backward(std::abs(throttle));
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool MotorController::move_distance_cm_pid(float distance_cm, float max_speed, int direction, uint32_t timeout_ms) {
    if (!encoders_enabled) {
        // 如果未启用，尝试自动启用
        if (!initEncoders()) {
            std::cerr << "[move_distance_cm_pid] ❌ 编码器未启用或未初始化" << std::endl;
            return false;
        }
        encoders_enabled = true;
    }
    if (!left_encoder || !right_encoder) {
        std::cerr << "[move_distance_cm_pid] ❌ 编码器对象为空" << std::endl;
        return false;
    }

    // ========== enable_pid 检查：如果禁用了 PID，使用分阶段速度策略 ==========
    if (!encoder_config_.enable_pid) {
        std::cout << "\n[move_distance_cm_pid] ⚠️ PID 已禁用，使用分阶段速度控制策略" << std::endl;
        return move_distance_cm_with_profile(distance_cm, max_speed, timeout_ms);
    }
    
    // 处理方向
    if (direction != 0) {
        std::cout << "[move_distance_cm_pid] 🔄 反向执行：距离反号" << std::endl;
        distance_cm = -distance_cm;
    }

    int32_t target_pulses = distance_to_pulses(distance_cm);
    
    std::cout << "\n[move_distance_cm_pid] ====== PID 位置控制开始 ======" << std::endl;
    std::cout << "[move_distance_cm_pid] 目标距离: " << distance_cm << " cm" << std::endl;
    std::cout << "[move_distance_cm_pid] 目标脉冲: " << target_pulses << std::endl;
    std::cout << "[move_distance_cm_pid] 最大速度: " << max_speed * 100.0f << "%" << std::endl;
    std::cout << "[move_distance_cm_pid] PID 参数: Kp=" << position_pid_config_.kp 
              << " Ki=" << position_pid_config_.ki 
              << " Kd=" << position_pid_config_.kd << std::endl;

    // 重置编码器和 PID 状态
    reset_encoders();
    {
        std::lock_guard<std::mutex> lock(pid_state_mutex_);
        left_pid_state_.reset();
        right_pid_state_.reset();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto start_time = std::chrono::high_resolution_clock::now();
    uint32_t last_log_ms = 0;
    int32_t tolerance = encoder_config_.position_tolerance;
    
    // 启动电机
    if (target_pulses > 0) {
        forward(max_speed);
    } else {
        backward(std::abs(max_speed));
    }

    std::cout << "[move_distance_cm_pid] 电机启动，开始 PID 控制循环..." << std::endl;

    // PID 控制循环
    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        uint32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

        // 超时检查
        if (elapsed_ms > timeout_ms) {
            std::cerr << "[move_distance_cm_pid] ❌ 超时 (" << elapsed_ms << "ms > " << timeout_ms << "ms)" << std::endl;
            stop();
            return false;
        }

        // 读取编码器位置
        int32_t left_pos = getLeftEncoderPosition();
        int32_t right_pos = getRightEncoderPosition();
        int32_t avg_pos = (left_pos + right_pos) / 2;

        // 计算误差
        int32_t error = target_pulses - avg_pos;

        // 到达检查（精确容限）
        if (std::abs(error) <= tolerance) {
            std::cout << "[move_distance_cm_pid] ✅ 到达目标！最终位置=" << avg_pos 
                      << " 目标=" << target_pulses << " 误差=" << error << std::endl;
            stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            
            // 验证最终位置
            int32_t final_left = getLeftEncoderPosition();
            int32_t final_right = getRightEncoderPosition();
            int32_t final_avg = (final_left + final_right) / 2;
            int32_t final_error = target_pulses - final_avg;
            
            if (std::abs(final_error) <= tolerance) {
                std::cout << "[move_distance_cm_pid] ✅ 最终验证通过：误差=" << final_error << std::endl;
                return true;
            } else {
                std::cout << "[move_distance_cm_pid] ⚠️ 停止后误差变化：" << final_error << std::endl;
                if (std::abs(final_error) <= tolerance * 3) {
                    return true;  // 误差在容限的3倍内接受（最多 ±6 脉冲）
                }
            }
        }
        
        // 近距离止振：当误差较小但还没完全到达时，停止并等待
        // 这防止了接近目标时的摇摆问题
        if (std::abs(error) <= 10 && elapsed_ms > 700) {
            std::cout << "[move_distance_cm_pid] ℹ️ 接近目标（误差=" << error << "脉冲），停止电机以防震荡" << std::endl;
            stop();
            std::this_thread::sleep_for(std::chrono::milliseconds(300));
            
            // 再次检查最终位置
            int32_t final_left = getLeftEncoderPosition();
            int32_t final_right = getRightEncoderPosition();
            int32_t final_avg = (final_left + final_right) / 2;
            int32_t final_error = target_pulses - final_avg;
            
            if (std::abs(final_error) <= tolerance * 3) {
                std::cout << "[move_distance_cm_pid] ✅ 接近停止成功：误差=" << final_error << std::endl;
                return true;
            }
        }

        // PID 计算（100ms 至少更新一次）
        if (elapsed_ms - last_log_ms >= position_pid_config_.control_period_ms) {
            float dt = (elapsed_ms - last_log_ms) / 1000.0f;  // 转换为秒
            
            // 读取编码器位置（在 PID 计算前）
            int32_t left_pos = getLeftEncoderPosition();
            int32_t right_pos = getRightEncoderPosition();
            int32_t avg_pos = (left_pos + right_pos) / 2;
            
            // 计算 PID 输出
            float output = calculatePID(error, dt, left_pid_state_, position_pid_config_);
            
            // 限制最大速度
            output = std::min(output, max_speed * 100.0f);
            
            // 应用输出（PWM 范围：0.1~1.0，即 10%~100%）
            float throttle = output / 100.0f;
            
            // PWM 范围检查和日志
            if (encoder_config_.debug_pulses) {
                std::cout << "[move_distance_cm_pid] t=" << std::setw(4) << elapsed_ms << "ms | "
                          << "L=" << std::setw(3) << left_pos << " R=" << std::setw(3) << right_pos 
                          << " avg=" << std::setw(3) << avg_pos << "/" << target_pulses 
                          << " err=" << std::setw(3) << error 
                          << " | PID_out=" << std::fixed << std::setprecision(1) << output << "% "
                          << "PWM=" << std::fixed << std::setprecision(2) << throttle 
                          << " [" << (error > 0 ? "FWD" : "BWD") << "]" << std::endl;
            }
            
            if (error > 0) {
                forward(throttle);
            } else {
                backward(std::abs(throttle));
            }
            
            last_log_ms = elapsed_ms;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool MotorController::turn_deg_pid(float angle_deg, float max_speed, uint32_t timeout_ms) {
    if (!encoders_enabled) {
        // 如果未启用，尝试自动启用
        if (!initEncoders()) {
            std::cerr << "[turn_deg_pid] ❌ 编码器未启用或未初始化" << std::endl;
            return false;
        }
        encoders_enabled = true;
    }
    if (!left_encoder || !right_encoder) {
        std::cerr << "[turn_deg_pid] ❌ 编码器对象为空" << std::endl;
        return false;
    }

    // ========== enable_pid 检查：如果禁用了 PID，使用普通转向 ==========
    if (!encoder_config_.enable_pid) {
        std::cout << "\n[turn_deg_pid] ⚠️ PID 已禁用，使用普通转向控制" << std::endl;
        return turn_deg(angle_deg, max_speed, timeout_ms);
    }

    // 计算目标脉冲差
    int32_t target_pulse_diff = angle_to_pulse_diff(angle_deg);
    
    std::cout << "\n[turn_deg_pid] ====== PID 位置控制转向开始 ======" << std::endl;
    std::cout << "[turn_deg_pid] 目标角度: " << angle_deg << "°" << std::endl;
    std::cout << "[turn_deg_pid] 目标脉冲差: " << target_pulse_diff << std::endl;
    std::cout << "[turn_deg_pid] 最大速度: " << max_speed * 100.0f << "%" << std::endl;

    // 重置编码器和 PID 状态
    reset_encoders();
    {
        std::lock_guard<std::mutex> lock(pid_state_mutex_);
        left_pid_state_.reset();
        right_pid_state_.reset();
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto start_time = std::chrono::high_resolution_clock::now();
    uint32_t last_log_ms = 0;
    int32_t tolerance = std::max(10, static_cast<int32_t>(std::abs(target_pulse_diff) * 0.05f));  // 5% 或 5 脉冲
    
    // 启动电机（差速转向）
    if (angle_deg > 0) {
        turnRight(max_speed);  // 顺时针
    } else {
        turnLeft(max_speed);   // 逆时针
    }

    std::cout << "[turn_deg_pid] 差速转向启动，开始 PID 控制循环..." << std::endl;

    // PID 控制循环
    while (true) {
        auto now = std::chrono::high_resolution_clock::now();
        uint32_t elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time).count();

        // 超时检查
        if (elapsed_ms > timeout_ms) {
            std::cerr << "[turn_deg_pid] ❌ 超时 (" << elapsed_ms << "ms > " << timeout_ms << "ms)" << std::endl;
            stop();
            return false;
        }

        // 读取编码器位置
        int32_t left_pos = getLeftEncoderPosition();
        int32_t right_pos = getRightEncoderPosition();
        int32_t pulse_diff = left_pos - right_pos;  // 脉冲差

        // 计算误差
        int32_t error = target_pulse_diff - pulse_diff;

        // 到达检查
        if (std::abs(error) <= tolerance) {
            std::cout << "[turn_deg_pid] ✅ 到达目标！脉冲差=" << pulse_diff 
                      << " 目标=" << target_pulse_diff << " 误差=" << error << std::endl;
            stop();
            return true;
        }

        // PID 计算（50ms 更新一次）
        if (elapsed_ms - last_log_ms >= position_pid_config_.control_period_ms) {
            float dt = (elapsed_ms - last_log_ms) / 1000.0f;
            
            // 计算 PID 输出
            float output = calculatePID(error, dt, left_pid_state_, position_pid_config_);
            output = std::min(output, max_speed * 100.0f);
            float throttle = output / 100.0f;
            
            // 死区检查
            if (std::abs(throttle) < 0.1f) {
                stop();
                if (encoder_config_.debug_pulses) {
                    std::cout << "[turn_deg_pid] ⚠️ PWM低于死区(0.1)，停止电机" << std::endl;
                }
            } else {
                // 差速控制
                if (angle_deg > 0) {
                    turnRight(throttle);
                } else {
                    turnLeft(throttle);
                }
                
                // 调试日志
                if (encoder_config_.debug_pulses) {
                    std::cout << "[turn_deg_pid] t=" << std::setw(4) << elapsed_ms << "ms | "
                              << "L=" << std::setw(3) << left_pos << " R=" << std::setw(3) << right_pos 
                              << " diff=" << std::setw(3) << pulse_diff << "/" << target_pulse_diff
                              << " err=" << std::setw(3) << error
                              << " | PID_out=" << std::fixed << std::setprecision(1) << output << "% "
                              << "PWM=" << std::fixed << std::setprecision(2) << throttle 
                              << " [" << (angle_deg > 0 ? "RIGHT" : "LEFT") << "]" << std::endl;
                }
            }
            
            last_log_ms = elapsed_ms;
            if (encoder_config_.debug_pulses) {
                std::cout << "[turn_deg_pid] t=" << elapsed_ms << "ms "
                          << "L=" << left_pos << " R=" << right_pos 
                          << " diff=" << pulse_diff << "/" << target_pulse_diff 
                          << " err=" << error << " out=" << output << "%" << std::endl;
            }
            
            last_log_ms = elapsed_ms;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

