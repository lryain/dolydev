#ifndef MOTOR_CONTROLLER_HPP
#define MOTOR_CONTROLLER_HPP

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include "sdk/DriveEvent.h"

class MotorController {
public:
    struct PositionPIDConfig {
        float kp = 0.2f;
        float ki = 0.01f;
        float kd = 0.01f;
        float ki_limit = 15.0f;
        float output_min = 10.0f;
        float output_max = 100.0f;
        std::uint32_t control_period_ms = 50;
    };

    struct EncoderConfig {
        int pulses_per_revolution = 1200;
        float wheel_diameter_cm = 3.0f;
        float wheel_circumference_cm = 9.4248f;
        float cm_per_pulse = 0.0157f;
        float axle_width_cm = 8.0f;
        float turn_compensation_factor = 1.0f;
        int position_tolerance = 50;
        float angle_tolerance_deg = 2.0f;
        bool enable_encoder = false;
        bool enable_pid = false;
        bool debug_encoder = false;
        bool debug_pulses = false;
        float pwm_deadzone = 0.1f;
        float pwm_min_active = 0.1f;
        float pwm_max = 1.0f;
    };

    struct MovePulsesResult {
        bool reached = false;
        long left_pulses = 0;
        long right_pulses = 0;
        double elapsed_time = 0.0;
    };

    MotorController(const std::string& i2c_dev = "/dev/i2c-3", int addr = 0x40);
    MotorController(const std::string& i2c_dev, int addr, const std::string& config_file);
    ~MotorController();

    bool init();

    void setSpeeds(float left, float right, float duration = -1.0f);
    void stop();
    void brake();
    void forward(float speed = 0.5f, float duration = -1.0f);
    void backward(float speed = 0.5f, float duration = -1.0f);
    void turnLeft(float speed = 0.5f, float duration = -1.0f);
    void turnRight(float speed = 0.5f, float duration = -1.0f);

    MovePulsesResult movePulses(long target_pulses, float throttle = 0.5f,
                                double assume_rate = 100.0, double timeout_multiplier = 3.0);
    MovePulsesResult turnPulses(long target_pulses, float throttle = 0.5f,
                                bool turn_left = true, double assume_rate = 100.0,
                                double timeout_multiplier = 3.0);

    bool move_distance_cm(float distance_cm, float throttle = 0.5f, std::uint32_t timeout_ms = 5000);
    bool turn_deg(float angle_deg, float throttle = 0.5f, std::uint32_t timeout_ms = 5000);
    bool go_xy(int16_t x, int16_t y, int speed, bool to_forward,
               bool with_brake = false, std::uint8_t acceleration_interval = 0,
               bool control_speed = false, bool control_force = true,
               std::uint32_t timeout_ms = 10000);
    bool go_distance(float distance_mm, int speed, bool to_forward,
                     bool with_brake = false, std::uint8_t acceleration_interval = 0,
                     bool control_speed = false, bool control_force = true,
                     std::uint32_t timeout_ms = 10000);
    bool go_rotate(float angle_deg, bool from_center, int speed, bool to_forward,
                   bool with_brake = false, std::uint8_t acceleration_interval = 0,
                   bool control_speed = false, bool control_force = true,
                   std::uint32_t timeout_ms = 8000);
    bool motor_stop_and_wait();

    std::int32_t get_left_encoder_value() const;
    std::int32_t get_right_encoder_value() const;
    void reset_encoders();

    bool load_encoder_config(const std::string& config_file = "/home/pi/dolydev/config/motor_config.ini");
    void load_pwm_compensation(const std::string& config_file = "/home/pi/dolydev/config/motor_config.ini");
    const EncoderConfig& get_encoder_config() const { return encoder_config_; }

    std::int32_t distance_to_pulses(float distance_cm) const;
    float pulses_to_distance(std::int32_t pulses) const;
    std::int32_t angle_to_pulse_diff(float angle_deg) const;

    void setContinuousMode(bool enabled);
    void setAutoStopTimeout(float timeout);

    static bool loadMotorConfig(bool& left_reverse, bool& right_reverse,
                                const std::string& config_file = "/home/pi/dolydev/config/motor_config.ini",
                                float* ramp_time_out = nullptr);
    bool loadMotorConfig(bool& left_reverse, bool& right_reverse);

    float getLeftSpeed() const { return left_speed_.load(); }
    float getRightSpeed() const { return right_speed_.load(); }
    bool isContinuousMode() const { return continuous_mode_.load(); }

    long getLeftEncoderPosition() const;
    long getRightEncoderPosition() const;
    long getLeftEncoderDelta();
    long getRightEncoderDelta();

    void enablePID(bool enabled);
    void setPIDParameters(double kp, double ki, double kd);
    void updateEncoderFeedback(double left_pos, double right_pos);
    bool isPIDEnabled() const { return pid_enabled_.load(); }

    void enableEncoders(bool enabled);
    bool initEncoders();
    void updateEncoderFeedbackFromHardware();
    void setEncoderDebugEnabled(bool enabled);
    bool isEncodersEnabled() const { return encoders_enabled_.load(); }
    bool hasLeftEncoder() const { return encoders_enabled_.load(); }
    bool hasRightEncoder() const { return encoders_enabled_.load(); }

    void enableSafetyMonitor(bool enabled);
    void enableSafety(bool enabled);
    bool initSafetyMonitor();
    void safetyCallback(const std::string& message);
    bool isSafe() const;
    bool isSafetyMonitorEnabled() const { return safety_enabled_.load(); }

    void enableCurrentMonitoring(bool enabled);
    bool isCurrentMonitoringEnabled() const { return current_monitoring_enabled_.load(); }

    void setPWM(int channel, int on, int off);
    void setRampTime(float seconds) { ramp_time_seconds_ = seconds; }
    float getRampTime() const { return ramp_time_seconds_; }

    void setPWMCompensation(float left, float right);
    float getLeftPWMCompensation() const { return left_pwm_compensation_; }
    float getRightPWMCompensation() const { return right_pwm_compensation_; }

    void setBalanceScalers(float left, float right);
    float getLeftScaler() const { return left_scaler_; }
    float getRightScaler() const { return right_scaler_; }

    void setPIDConfig(const PositionPIDConfig& config);
    const PositionPIDConfig& getPIDConfig() const { return position_pid_config_; }

    bool move_distance_cm_pid(float distance_cm, float max_speed = 0.5f, int direction = 0,
                              std::uint32_t timeout_ms = 5000);
    bool turn_deg_pid(float angle_deg, float max_speed = 0.3f, std::uint32_t timeout_ms = 5000);
    bool move_distance_cm_with_profile(float distance_cm, float max_speed, std::uint32_t timeout_ms);

    bool drive_distance(float distance_mm, int speed, int accel, int brake, int direction,
                        std::uint32_t timeout_ms = 10000);
    bool drive_rotate(float angle_deg, int speed, bool is_center_turn,
                      std::uint32_t timeout_ms = 8000);
    bool drive_rotate_left(float angle_deg, int speed, bool is_center_turn,
                           std::uint32_t timeout_ms = 8000) {
        return drive_rotate(-std::abs(angle_deg), speed, is_center_turn, timeout_ms);
    }
    bool drive_rotate_right(float angle_deg, int speed, bool is_center_turn,
                            std::uint32_t timeout_ms = 8000) {
        return drive_rotate(std::abs(angle_deg), speed, is_center_turn, timeout_ms);
    }

    bool drive_distance_pid(float distance_mm, int speed, int accel, int brake, int direction,
                            std::uint32_t timeout_ms = 10000);
    bool turn_deg_pid_advanced(float angle_deg, int speed, bool is_center_turn,
                               std::uint32_t timeout_ms = 8000);

private:
    struct PendingCommand {
        std::uint16_t id = 0;
        bool in_flight = false;
        bool completed = false;
        bool failed = false;
    };

    struct WheelPose {
        float linear_cm = 0.0f;
        float angular_cm = 0.0f;
        long left_pulses = 0;
        long right_pulses = 0;
    };

    void registerEventCallbacks();
    void unregisterEventCallbacks();
    bool executeDistanceCommand(float distance_mm, std::uint8_t speed_percent, bool to_forward,
                                bool with_brake, std::uint8_t acceleration_interval,
                                bool control_speed, bool control_force,
                                std::uint32_t timeout_ms);
    bool executeRotateCommand(float angle_deg, bool from_center, std::uint8_t speed_percent,
                              bool to_forward, bool with_brake, std::uint8_t acceleration_interval,
                              bool control_speed, bool control_force,
                              std::uint32_t timeout_ms);
    bool waitForPendingCommand(std::uint16_t id, std::uint32_t timeout_ms);
    std::uint16_t nextCommandId();
    void applyManualDrive(float left, float right);
    void setWheelSpeed(bool is_left, float value);
    void capturePoseBaseline();
    WheelPose computeWheelPose() const;
    void handleDriveComplete(std::uint16_t id);
    void handleDriveError(std::uint16_t id);
    static void onDriveCompleteStatic(std::uint16_t id);
    static void onDriveErrorStatic(std::uint16_t id, DriveMotorSide side, DriveErrorType type);
    static void onDriveStateChangeStatic(DriveType drive_type, DriveState state);
    void startAutoStopTimer(float duration);
    void cancelAutoStopTimer();
    void autoStopWorker(float duration);

    
    
    

    std::atomic<bool> initialized_{false};
    std::atomic<bool> continuous_mode_{false};
    std::atomic<float> auto_stop_timeout_{2.0f};
    std::thread auto_stop_thread_;
    std::atomic<bool> auto_stop_cancel_{false};

    std::atomic<float> left_speed_{0.0f};
    std::atomic<float> right_speed_{0.0f};
    float ramp_time_seconds_ = 0.2f;
    bool left_motor_reverse_ = false;
    bool right_motor_reverse_ = false;
    float left_pwm_compensation_ = 1.0f;
    float right_pwm_compensation_ = 1.0f;
    float left_scaler_ = 1.0f;
    float right_scaler_ = 1.0f;

    std::atomic<bool> pid_enabled_{false};
    std::atomic<bool> encoders_enabled_{false};
    std::atomic<bool> safety_enabled_{false};
    std::atomic<bool> current_monitoring_enabled_{false};
    std::atomic<bool> safe_state_{true};

    EncoderConfig encoder_config_;
    PositionPIDConfig position_pid_config_;

    mutable std::mutex pose_mutex_;
    float baseline_x_mm_ = 0.0f;
    float baseline_y_mm_ = 0.0f;
    float baseline_head_deg_ = 0.0f;

    mutable std::mutex encoder_delta_mutex_;
    mutable long last_left_encoder_report_ = 0;
    mutable long last_right_encoder_report_ = 0;

    std::mutex init_mutex_;
    std::mutex drive_mutex_;
    std::mutex command_mutex_;
    std::condition_variable command_cv_;
    PendingCommand pending_command_;
    std::atomic<std::uint16_t> next_command_id_{1};
    bool callbacks_registered_ = false;

    static std::mutex callback_registry_mutex_;
    static MotorController* active_instance_;
};

#endif  // MOTOR_CONTROLLER_HPP