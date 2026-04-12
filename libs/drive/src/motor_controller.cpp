#include "drive/motor_controller.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <cmath>
#include <algorithm>
#include "sdk/DriveControl.h"
#include "sdk/DriveEvent.h"
#include "spdlog/spdlog.h"

MotorController* MotorController::active_instance_ = nullptr;
std::mutex MotorController::callback_registry_mutex_;

MotorController::MotorController(const std::string& i2c_dev, int addr) {
    active_instance_ = this;
    load_pwm_compensation(); // Load default config
}

MotorController::MotorController(const std::string& i2c_dev, int addr, const std::string& config_file) {
    active_instance_ = this;
    load_pwm_compensation(config_file);
}

MotorController::~MotorController() {
    unregisterEventCallbacks();
    DriveControl::dispose(false);
}

void MotorController::registerEventCallbacks() {
    DriveEvent::AddListenerOnComplete(MotorController::onDriveCompleteStatic);
    DriveEvent::AddListenerOnError(MotorController::onDriveErrorStatic);
    DriveEvent::AddListenerOnStateChange(MotorController::onDriveStateChangeStatic);
    callbacks_registered_ = true;
}

void MotorController::unregisterEventCallbacks() {
    if(callbacks_registered_) {
        DriveEvent::RemoveListenerOnComplete(MotorController::onDriveCompleteStatic);
        DriveEvent::RemoveListenerOnError(MotorController::onDriveErrorStatic);
        DriveEvent::RemoveListenerOnStateChange(MotorController::onDriveStateChangeStatic);
        callbacks_registered_ = false;
    }
}

bool MotorController::init() {
    std::cout << "[MotorController] Initializing..." << std::endl;
    registerEventCallbacks();
    initialized_ = (DriveControl::init() == 0);
    std::cout << "[MotorController] initialized=" << (initialized_ ? "YES" : "NO") << " left_comp=" << left_pwm_compensation_ << " right_comp=" << right_pwm_compensation_ << std::endl;
    return initialized_;
}

void MotorController::setSpeeds(float left, float right, float duration) {
    left_speed_ = left;
    right_speed_ = right;
    
    // Apply PWM Compensation (multiplier)
    float comp_left = left * left_pwm_compensation_;
    float comp_right = right * right_pwm_compensation_;
    
    // 手动运动在 0.2~0.3 这类低占空比下容易克服不了静摩擦，这里给非零请求一个底限。
    auto normalize_manual_speed = [](float requested, float compensated) -> uint8_t {
        if (std::abs(requested) < 1e-4f) {
            return 0;
        }

        int speed_pc = static_cast<int>(std::lround(std::min(100.0f, std::abs(compensated) * 100.0f)));
        if (speed_pc > 0 && speed_pc < 25) {
            speed_pc = 25;
        }
        return static_cast<uint8_t>(std::clamp(speed_pc, 0, 100));
    };

    uint8_t left_pc = normalize_manual_speed(left, comp_left);
    uint8_t right_pc = normalize_manual_speed(right, comp_right);
    printf("[MotorController] setSpeeds left=%.2f (comp=%.2f pc=%d) right=%.2f (comp=%.2f pc=%d) duration=%.2fs\n",
           left, comp_left, left_pc, right, comp_right, right_pc, duration);
    DriveControl::freeDrive(left_pc, true, left >= 0);
    DriveControl::freeDrive(right_pc, false, right >= 0);
    if(duration > 0.0f) startAutoStopTimer(duration);
}

void MotorController::stop() {
    left_speed_ = 0.0f;
    right_speed_ = 0.0f;
    DriveControl::freeDrive(0, true, true);
    DriveControl::freeDrive(0, false, true);
}

void MotorController::brake() { stop(); }
void MotorController::forward(float speed, float duration) { setSpeeds(speed, speed, duration); }
void MotorController::backward(float speed, float duration) { setSpeeds(-speed, -speed, duration); }
void MotorController::turnLeft(float speed, float duration) { setSpeeds(-speed, speed, duration); }
void MotorController::turnRight(float speed, float duration) { setSpeeds(speed, -speed, duration); }

std::uint16_t MotorController::nextCommandId() { return next_command_id_++; }

bool MotorController::waitForPendingCommand(std::uint16_t id, std::uint32_t timeout_ms) {
    std::unique_lock<std::mutex> lock(command_mutex_);
    auto result = command_cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms),
        [this, id] { return !pending_command_.in_flight || pending_command_.id != id; });
    return pending_command_.completed;
}

MotorController::MovePulsesResult MotorController::movePulses(long target_pulses, float throttle, double assume_rate, double timeout_multiplier) {
    MovePulsesResult res;
    float dist_cm = pulses_to_distance(target_pulses);
    res.reached = move_distance_cm(dist_cm, throttle);
    return res;
}

MotorController::MovePulsesResult MotorController::turnPulses(long target_pulses, float throttle, bool turn_left, double assume_rate, double timeout_multiplier) {
    MovePulsesResult res;
    float dist_cm = pulses_to_distance(target_pulses);
    float angle_deg = (dist_cm / encoder_config_.wheel_circumference_cm) * 360.0f * (turn_left ? -1.0f : 1.0f);
    res.reached = turn_deg(angle_deg, throttle);
    return res;
}

bool MotorController::move_distance_cm(float distance_cm, float throttle, std::uint32_t timeout_ms) {
    std::uint16_t id = nextCommandId();
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        pending_command_ = {id, true, false, false};
    }
    
    // throttle typically 0.0 ~ 1.0. Apply compensation similarly.
    float compensated_throttle = std::abs(throttle) * ((left_pwm_compensation_ + right_pwm_compensation_) / 2.0f);
    int speed_pc = static_cast<int>(std::lround(std::min(100.0f, compensated_throttle * 100.0f)));
    if (speed_pc > 0 && speed_pc < 25) {
        speed_pc = 25;
    }
    
    DriveControl::goDistance(id, std::abs(distance_cm * 10), static_cast<std::uint8_t>(speed_pc), distance_cm >= 0, true);
    return waitForPendingCommand(id, timeout_ms);
}

bool MotorController::go_distance(float distance_mm, int speed, bool to_forward,
                                  bool with_brake, std::uint8_t acceleration_interval,
                                  bool control_speed, bool control_force,
                                  std::uint32_t timeout_ms) {
    std::uint16_t id = nextCommandId();
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        pending_command_ = {id, true, false, false};
    }

    float compensation = (left_pwm_compensation_ + right_pwm_compensation_) * 0.5f;
    int compensated_speed = static_cast<int>(std::lround(std::abs(speed) * compensation));
    if (compensated_speed > 0 && compensated_speed < 20) {
        compensated_speed = 20;
    }
    compensated_speed = std::clamp(compensated_speed, 0, 100);

    DriveControl::goDistance(id, std::abs(distance_mm), static_cast<std::uint8_t>(compensated_speed),
                             to_forward, with_brake, acceleration_interval, control_speed, control_force);
    return waitForPendingCommand(id, timeout_ms);
}

bool MotorController::turn_deg(float angle_deg, float throttle, std::uint32_t timeout_ms) {
    int requested_speed = static_cast<int>(std::lround(std::min(100.0f, std::abs(throttle) * 150.0f)));
    if (requested_speed > 0 && requested_speed < 25) {
        requested_speed = 25;
    }

    return go_rotate(angle_deg, true, requested_speed, angle_deg >= 0, true, 0, false, true, timeout_ms);
}

bool MotorController::go_xy(int16_t x, int16_t y, int speed, bool to_forward,
                            bool with_brake, std::uint8_t acceleration_interval,
                            bool control_speed, bool control_force,
                            std::uint32_t timeout_ms) {
    std::uint16_t id = nextCommandId();
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        pending_command_ = {id, true, false, false};
    }

    float compensation = (left_pwm_compensation_ + right_pwm_compensation_) * 0.5f;
    int compensated_speed = static_cast<int>(std::lround(std::abs(speed) * compensation));
    if (compensated_speed > 0 && compensated_speed < 20) {
        compensated_speed = 20;
    }
    compensated_speed = std::clamp(compensated_speed, 0, 100);

    DriveControl::goXY(id, x, y, static_cast<std::uint8_t>(compensated_speed), to_forward,
                       with_brake, acceleration_interval, control_speed, control_force);
    return waitForPendingCommand(id, timeout_ms);
}

bool MotorController::go_rotate(float angle_deg, bool from_center, int speed, bool to_forward,
                                bool with_brake, std::uint8_t acceleration_interval,
                                bool control_speed, bool control_force,
                                std::uint32_t timeout_ms) {
    std::uint16_t id = nextCommandId();
    {
        std::lock_guard<std::mutex> lock(command_mutex_);
        pending_command_ = {id, true, false, false};
    }

    float compensation = (left_pwm_compensation_ + right_pwm_compensation_) * 0.5f;
    int compensated_speed = static_cast<int>(std::lround(std::abs(speed) * compensation));
    if (compensated_speed > 0 && compensated_speed < 25) {
        compensated_speed = 25;
    }
    compensated_speed = std::clamp(compensated_speed, 0, 100);

    DriveControl::goRotate(id, std::abs(angle_deg), from_center, static_cast<std::uint8_t>(compensated_speed),
                           to_forward, with_brake, acceleration_interval, control_speed, control_force);
    return waitForPendingCommand(id, timeout_ms);
}

bool MotorController::motor_stop_and_wait() {
    stop();
    return true;
}

std::int32_t MotorController::get_left_encoder_value() const { return 0; }
std::int32_t MotorController::get_right_encoder_value() const { return 0; }
void MotorController::reset_encoders() { DriveControl::resetPosition(); }
bool MotorController::load_encoder_config(const std::string& config_file) { return true; }

void MotorController::load_pwm_compensation(const std::string& config_file) {
    std::ifstream file(config_file);
    if (!file.is_open()) {
        std::cerr << "[MotorController] ⚠️ Failed to open config: " << config_file << std::endl;
        return;
    }

    std::string line;
    while (std::getline(file, line)) {
        // Simple INI parsing
        line.erase(0, line.find_first_not_of(" \t"));
        if (line.empty() || line[0] == '#' || line[0] == ';') continue;

        size_t sep = line.find('=');
        if (sep == std::string::npos) continue;

        std::string key = line.substr(0, sep);
        std::string val = line.substr(sep + 1);

        // Trim whitespace
        key.erase(key.find_last_not_of(" \t") + 1);
        val.erase(0, val.find_first_not_of(" \t"));
        val.erase(val.find_last_not_of(" \t") + 1);

        if (key == "left_pwm_compensation") {
            left_pwm_compensation_ = std::stof(val);
            std::cout << "[MotorController] Left compensation: " << left_pwm_compensation_ << std::endl;
        } else if (key == "right_pwm_compensation") {
            right_pwm_compensation_ = std::stof(val);
            std::cout << "[MotorController] Right compensation: " << right_pwm_compensation_ << std::endl;
        }
    }
}

void MotorController::setPWMCompensation(float left, float right) {
    left_pwm_compensation_ = left;
    right_pwm_compensation_ = right;
}

void MotorController::setBalanceScalers(float left, float right) {}
void MotorController::setPIDConfig(const PositionPIDConfig& config) {}

std::int32_t MotorController::distance_to_pulses(float distance_cm) const { return (std::int32_t)(distance_cm / encoder_config_.cm_per_pulse); }
float MotorController::pulses_to_distance(std::int32_t pulses) const { return pulses * encoder_config_.cm_per_pulse; }
std::int32_t MotorController::angle_to_pulse_diff(float angle_deg) const {
    float dist = (std::abs(angle_deg) / 360.0f) * encoder_config_.wheel_circumference_cm;
    return distance_to_pulses(dist);
}

void MotorController::setContinuousMode(bool enabled) { continuous_mode_ = enabled; }
void MotorController::setAutoStopTimeout(float timeout) { auto_stop_timeout_ = timeout; }
bool MotorController::loadMotorConfig(bool& left_reverse, bool& right_reverse, const std::string& config_file, float* ramp_time_out) {
    left_reverse = false; right_reverse = false;
    if(ramp_time_out) *ramp_time_out = 0.2f;
    return true;
}
bool MotorController::loadMotorConfig(bool& left_reverse, bool& right_reverse) { return true; }
long MotorController::getLeftEncoderPosition() const { return 0; }
long MotorController::getRightEncoderPosition() const { return 0; }
long MotorController::getLeftEncoderDelta() { return 0; }
long MotorController::getRightEncoderDelta() { return 0; }
void MotorController::enablePID(bool enabled) { pid_enabled_ = enabled; }
void MotorController::setPIDParameters(double kp, double ki, double kd) {}
void MotorController::updateEncoderFeedback(double left_pos, double right_pos) {}
void MotorController::enableEncoders(bool enabled) { encoders_enabled_ = enabled; }
bool MotorController::initEncoders() { return true; }
void MotorController::updateEncoderFeedbackFromHardware() {}
void MotorController::setEncoderDebugEnabled(bool enabled) {}
void MotorController::enableSafetyMonitor(bool enabled) { safety_enabled_ = enabled; }
void MotorController::enableSafety(bool enabled) {}
bool MotorController::initSafetyMonitor() { return true; }
void MotorController::safetyCallback(const std::string& message) {}
bool MotorController::isSafe() const { return safe_state_; }
void MotorController::enableCurrentMonitoring(bool enabled) {}
void MotorController::setPWM(int channel, int on, int off) {}

bool MotorController::move_distance_cm_pid(float distance_cm, float max_speed, int direction, std::uint32_t timeout_ms) {
    return move_distance_cm(distance_cm, max_speed, timeout_ms);
}

bool MotorController::turn_deg_pid(float angle_deg, float max_speed, std::uint32_t timeout_ms) {
    return turn_deg(angle_deg, max_speed, timeout_ms);
}

bool MotorController::move_distance_cm_with_profile(float distance_cm, float max_speed, std::uint32_t timeout_ms) {
    return move_distance_cm(distance_cm, max_speed, timeout_ms);
}

void MotorController::onDriveCompleteStatic(std::uint16_t id) {
    if(active_instance_) active_instance_->handleDriveComplete(id);
}

void MotorController::onDriveErrorStatic(std::uint16_t id, DriveMotorSide side, DriveErrorType type) {
    if(active_instance_) active_instance_->handleDriveError(id);
}

void MotorController::onDriveStateChangeStatic(DriveType drive_type, DriveState state) {}

void MotorController::handleDriveComplete(std::uint16_t id) {
    std::lock_guard<std::mutex> lock(command_mutex_);
    if(pending_command_.in_flight && pending_command_.id == id) {
        pending_command_.in_flight = false;
        pending_command_.completed = true;
        command_cv_.notify_all();
    }
}

void MotorController::handleDriveError(std::uint16_t id) {
    std::lock_guard<std::mutex> lock(command_mutex_);
    if(pending_command_.in_flight && pending_command_.id == id) {
        pending_command_.in_flight = false;
        pending_command_.failed = true;
        command_cv_.notify_all();
    }
}

void MotorController::startAutoStopTimer(float duration) {
    cancelAutoStopTimer();
    auto_stop_cancel_ = false;
    auto_stop_thread_ = std::thread(&MotorController::autoStopWorker, this, duration);
    auto_stop_thread_.detach();
}

void MotorController::cancelAutoStopTimer() { auto_stop_cancel_ = true; }

void MotorController::autoStopWorker(float duration) {
    int steps = (int)(duration * 10);
    for(int i=0; i<steps; i++) {
        if(auto_stop_cancel_) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if(!auto_stop_cancel_) stop();
}

bool MotorController::executeDistanceCommand(float distance_mm, std::uint8_t speed_percent, bool to_forward,
                                bool with_brake, std::uint8_t acceleration_interval,
                                bool control_speed, bool control_force,
                                std::uint32_t timeout_ms) {
    return go_distance(distance_mm, speed_percent, to_forward,
                       with_brake, acceleration_interval, control_speed,
                       control_force, timeout_ms);
}

bool MotorController::executeRotateCommand(float angle_deg, bool from_center, std::uint8_t speed_percent,
                              bool to_forward, bool with_brake, std::uint8_t acceleration_interval,
                              bool control_speed, bool control_force,
                              std::uint32_t timeout_ms) {
    return go_rotate(angle_deg, from_center, speed_percent, to_forward,
                     with_brake, acceleration_interval, control_speed, control_force, timeout_ms);
}

void MotorController::applyManualDrive(float left, float right) {}
void MotorController::setWheelSpeed(bool is_left, float value) {}
void MotorController::capturePoseBaseline() {}
MotorController::WheelPose MotorController::computeWheelPose() const { return WheelPose(); }

// ---------------------------------------------------------
// Animation API Merged from motor_controller_animation_api.cpp
// ---------------------------------------------------------

bool MotorController::drive_distance(float distance_mm, int speed, int accel, int brake, int direction, std::uint32_t timeout_ms) {
    bool to_forward = (direction == 0);
    return go_distance(distance_mm, speed, to_forward, brake != 0,
                       static_cast<std::uint8_t>(accel), false, true, timeout_ms);
}

bool MotorController::drive_rotate(float angle_deg, int speed, bool is_center_turn, std::uint32_t timeout_ms) {
    if (is_center_turn) {
        int boosted_speed = static_cast<int>(std::lround(std::abs(speed) * 1.5f));
        if (boosted_speed > 0 && boosted_speed < 25) {
            boosted_speed = 25;
        }
        return go_rotate(angle_deg, true, boosted_speed, angle_deg >= 0, true, 0, false, true, timeout_ms);
    }

    return go_rotate(angle_deg, false, speed, angle_deg >= 0, true, 0, false, true, timeout_ms);
}

bool MotorController::drive_distance_pid(float distance_mm, int speed, int accel, int brake, int direction, std::uint32_t timeout_ms) {
    return drive_distance(distance_mm, speed, accel, brake, direction, timeout_ms);
}

bool MotorController::turn_deg_pid_advanced(float angle_deg, int speed, bool is_center_turn, std::uint32_t timeout_ms) {
    return drive_rotate(angle_deg, speed, is_center_turn, timeout_ms);
}
