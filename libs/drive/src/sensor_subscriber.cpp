/**
 * @file sensor_subscriber.cpp
 * @brief 传感器数据订阅实现
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "drive/sensor_subscriber.hpp"
#include <zmq.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <cstring>
#include <algorithm>

using json = nlohmann::json;

#define LOG_PREFIX "[ExtIOService]"

namespace doly::drive {

bool SensorLogConfig::should_log(Config& cfg) {
    if (!cfg.enabled || !cfg.debug) return false;
    if (cfg.log_interval_ms == 0) return true;
    uint64_t now = SharedState::getCurrentTimeMs();
    if (now - cfg.last_log_time_ms >= cfg.log_interval_ms) {
        cfg.last_log_time_ms = now;
        return true;
    }
    return false;
}

bool SensorLogConfig::should_log_raw(Config& cfg) {
    if (!cfg.enabled || !cfg.debug_raw) return false;
    if (cfg.log_interval_ms == 0) return true;
    uint64_t now = SharedState::getCurrentTimeMs();
    if (now - cfg.last_raw_time_ms >= cfg.log_interval_ms) {
        cfg.last_raw_time_ms = now;
        return true;
    }
    return false;
}

SensorSubscriber::SensorSubscriber(SharedState* shared_state, const SensorLogConfig& config)
    : shared_state_(shared_state), config_(config) {
    init_filters();
}

void SensorSubscriber::init_filters() {
    // TOF 滤波器初始化: 中值(5) + 低通(0.3)
    tof_left_filter_ = std::make_unique<filters::ChainFilter>();
    tof_left_filter_->add_filter(new filters::MedianFilter(5));
    tof_left_filter_->add_filter(new filters::LowPassFilter(0.3f));
    
    tof_right_filter_ = std::make_unique<filters::ChainFilter>();
    tof_right_filter_->add_filter(new filters::MedianFilter(5));
    tof_right_filter_->add_filter(new filters::LowPassFilter(0.3f));

    // TOF 异常检测: 无效值 255, 最大变化 200mm
    filters::OutlierDetector::Config tof_cfg;
    tof_cfg.invalid_value = 255.0f;
    tof_cfg.max_change = 200.0f;
    tof_left_detector_ = filters::OutlierDetector(tof_cfg);
    tof_right_detector_ = filters::OutlierDetector(tof_cfg);

    // IMU 滤波器初始化: 卡尔曼滤波 (Q=0.01, R=0.25)
    imu_roll_filter_ = std::make_unique<filters::KalmanFilter1D>(0.01f, 0.25f);
    imu_pitch_filter_ = std::make_unique<filters::KalmanFilter1D>(0.01f, 0.25f);
    imu_yaw_filter_ = std::make_unique<filters::KalmanFilter1D>(0.01f, 0.25f);

    // IMU 异常检测: 最大变化 30度
    filters::OutlierDetector::Config imu_cfg;
    imu_cfg.max_change = 30.0f;
    imu_detector_ = filters::OutlierDetector(imu_cfg);
    
    // 电源滤波器初始化: 窗口大小 20 的移动平均
    power_voltage_filter_ = std::make_unique<filters::MovingAverageFilter>(20);
    power_current_filter_ = std::make_unique<filters::MovingAverageFilter>(10);
    
    // 采样率设置
    tof_limiter_.set_rate(20.0f);
    imu_ahrs_limiter_.set_rate(50.0f);
    power_limiter_.set_rate(5.0f);
}

SensorSubscriber::~SensorSubscriber() {
    stop();
}

bool SensorSubscriber::start() {
    if (running_.load()) {
        std::cerr << LOG_PREFIX << " Sensor subscriber already running" << std::endl;
        return false;
    }
    
    running_ = true;
    
    tof_thread_ = std::thread(&SensorSubscriber::tof_subscriber_loop, this);
    imu_thread_ = std::thread(&SensorSubscriber::imu_subscriber_loop, this);
    power_thread_ = std::thread(&SensorSubscriber::power_subscriber_loop, this);
    
    std::cout << LOG_PREFIX << " ✅ Sensor subscriber started (TOF, IMU, Power)" << std::endl;
    return true;
}

void SensorSubscriber::stop() {
    if (!running_.load()) {
        return;
    }
    
    running_ = false;
    
    if (tof_thread_.joinable()) {
        tof_thread_.join();
    }
    if (imu_thread_.joinable()) {
        imu_thread_.join();
    }
    if (power_thread_.joinable()) {
        power_thread_.join();
    }
    
    std::cout << LOG_PREFIX << " ✅ Sensor subscriber stopped" << std::endl;
}

void SensorSubscriber::tof_subscriber_loop() {
    void* context = zmq_ctx_new();
    void* socket = zmq_socket(context, ZMQ_SUB);
    
    zmq_connect(socket, "ipc:///tmp/doly_sensor_pub.sock");
    
    const char* tof_topic = "status.sensor.tof";
    zmq_setsockopt(socket, ZMQ_SUBSCRIBE, tof_topic, strlen(tof_topic));
    
    int timeout = 1000;
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    std::cout << LOG_PREFIX << " [SENSOR] TOF subscriber thread started" << std::endl;
    
    char topic_buf[256];
    char data_buf[1024];
    uint64_t receive_count = 0;
    
    while (running_.load()) {
        int topic_size = zmq_recv(socket, topic_buf, sizeof(topic_buf) - 1, 0);
        if (topic_size < 0) continue;
        topic_buf[topic_size] = '\0';
        
        int data_size = zmq_recv(socket, data_buf, sizeof(data_buf) - 1, 0);
        if (data_size < 0) continue;
        data_buf[data_size] = '\0';
        
        receive_count++;

        try {
            json data_obj = json::parse(data_buf);
            
            // 1. 采样率控制
            uint64_t now_us = data_obj.value("ts_us", (uint64_t)SharedState::getCurrentTimeMs() * 1000);
            if (!tof_limiter_.should_sample(now_us)) {
                continue;
            }
            
            if (config_.should_log_raw(config_.tof)) {
                std::cerr << "[RAW-TOF] #" << receive_count << " Raw JSON: " << data_buf << std::endl;
            }
            
            if (data_obj.contains("payload") && data_obj["payload"].is_object()) {
                data_obj = data_obj["payload"];
            }
            
            float raw_left = data_obj.value("left", 255.0f);
            float raw_right = data_obj.value("right", 255.0f);
            
            // 2. 异常值检测与滤波处理
            float filtered_left, filtered_right;
            
            // 处理左侧
            tof_left_detector_.detect(raw_left, filtered_left);
            filtered_left = tof_left_filter_->update(filtered_left);
            
            // 处理右侧
            tof_right_detector_.detect(raw_right, filtered_right);
            filtered_right = tof_right_filter_->update(filtered_right);

            if (config_.should_log(config_.tof)) {
                std::cout << LOG_PREFIX << " 📏 ToF #" << receive_count 
                          << ": L_raw=" << raw_left << " L_filt=" << (int)filtered_left 
                          << ", R_raw=" << raw_right << " R_filt=" << (int)filtered_right << std::endl;
            }
            
            if (shared_state_) {
                shared_state_->tof.left_mm = static_cast<uint16_t>(filtered_left);
                shared_state_->tof.right_mm = static_cast<uint16_t>(filtered_right);
                shared_state_->tof.left_valid = (raw_left < 255);
                shared_state_->tof.right_valid = (raw_right < 255);
                shared_state_->tof.update_time_ms = SharedState::getCurrentTimeMs();
                
                uint16_t min_dist = std::min(
                    (filtered_left < 254) ? (uint16_t)filtered_left : (uint16_t)9999,
                    (filtered_right < 254) ? (uint16_t)filtered_right : (uint16_t)9999
                );
                shared_state_->tof.min_distance_mm = min_dist;
                
                if (min_dist < 150) {
                    shared_state_->tof.event_type = 1;
                    shared_state_->tof.obstacle_detected = true;
                } else if (min_dist < 500) {
                    shared_state_->tof.event_type = 2;
                    shared_state_->tof.obstacle_detected = false;
                } else {
                    shared_state_->tof.event_type = 3;
                    shared_state_->tof.obstacle_detected = false;
                }
            }
        } catch (const std::exception&) {
            // ignore parse errors
        }
    }
    
    zmq_close(socket);
    zmq_ctx_destroy(context);
    
    std::cout << LOG_PREFIX << " [SENSOR] TOF subscriber stopped" << std::endl;
}

void SensorSubscriber::imu_subscriber_loop() {
    void* context = zmq_ctx_new();
    void* socket = zmq_socket(context, ZMQ_SUB);
    
    zmq_connect(socket, "ipc:///tmp/doly_sensor_pub.sock");
    
    const char* ahrs_topic = "status.sensor.ahrs";
    zmq_setsockopt(socket, ZMQ_SUBSCRIBE, ahrs_topic, strlen(ahrs_topic));
    
    const char* gesture_topic = "status.sensor.gesture";
    zmq_setsockopt(socket, ZMQ_SUBSCRIBE, gesture_topic, strlen(gesture_topic));
    
    int timeout = 1000;
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    std::cout << LOG_PREFIX << " [SENSOR] IMU subscriber thread started" << std::endl;
    
    char topic_buf[256];
    char data_buf[1024];
    uint64_t ahrs_count = 0, gesture_count = 0;
    
    while (running_.load()) {
        int topic_size = zmq_recv(socket, topic_buf, sizeof(topic_buf) - 1, 0);
        if (topic_size < 0) continue;
        topic_buf[topic_size] = '\0';
        std::string topic(topic_buf, topic_size);
        
        int data_size = zmq_recv(socket, data_buf, sizeof(data_buf) - 1, 0);
        if (data_size < 0) continue;
        data_buf[data_size] = '\0';
        
        try {
            json data_obj = json::parse(data_buf);
            
            if (data_obj.contains("payload") && data_obj["payload"].is_object()) {
                data_obj = data_obj["payload"];
            }
            
            if (topic == "status.sensor.ahrs") {
                // 1. 采样率控制
                uint64_t now_us = data_obj.value("ts_us", (uint64_t)SharedState::getCurrentTimeMs() * 1000);
                if (!imu_ahrs_limiter_.should_sample(now_us)) {
                    continue;
                }

                ahrs_count++;
                float raw_roll = data_obj.value("roll", 0.0f);
                float raw_pitch = data_obj.value("pitch", 0.0f);
                float raw_yaw = data_obj.value("yaw", 0.0f);
                
                // 2. 滤波处理
                float filt_roll, filt_pitch, filt_yaw;
                
                imu_detector_.detect(raw_roll, filt_roll);
                filt_roll = imu_roll_filter_->update(filt_roll);
                
                imu_detector_.detect(raw_pitch, filt_pitch);
                filt_pitch = imu_pitch_filter_->update(filt_pitch);
                
                imu_detector_.detect(raw_yaw, filt_yaw);
                filt_yaw = imu_yaw_filter_->update(filt_yaw);

                std::string orientation = data_obj.value("orientation", "Unknown");
                
                if (config_.should_log(config_.ahrs)) {
                    std::cout << LOG_PREFIX << " 🧭 AHRS #" << ahrs_count 
                              << ": roll=" << (int)filt_roll << "°, pitch=" << (int)filt_pitch 
                              << "°, yaw=" << (int)filt_yaw << "°" << std::endl;
                }
                
                if (shared_state_) {
                    shared_state_->imu.roll = filt_roll;
                    shared_state_->imu.pitch = filt_pitch;
                    shared_state_->imu.yaw = filt_yaw;
                    
                    uint8_t orientation_type = 0;
                    if (orientation == "X-UP") orientation_type = 1;
                    else if (orientation == "X-DOWN") orientation_type = 2;
                    else if (orientation == "Y-UP") orientation_type = 3;
                    else if (orientation == "Y-DOWN") orientation_type = 4;
                    else if (orientation == "Z-UP") orientation_type = 5;
                    else if (orientation == "Z-DOWN") orientation_type = 6;
                    
                    shared_state_->imu.orientation = orientation_type;
                    shared_state_->imu.update_time_ms = SharedState::getCurrentTimeMs();
                }
            } else if (topic == "status.sensor.gesture") {
                gesture_count++;
                std::string gesture = data_obj.value("gesture", "Unknown");
                
                if (config_.should_log(config_.gesture)) {
                    std::cout << LOG_PREFIX << " 👆 Gesture #" << gesture_count << ": " << gesture << std::endl;
                }
                
                if (shared_state_) {
                    uint8_t gesture_type = 0;
                    if (gesture == "Nod") gesture_type = 0;
                    else if (gesture == "Shake") gesture_type = 1;
                    else if (gesture == "Stationary") gesture_type = 2;
                    else if (gesture == "Swing") gesture_type = 3;
                    else if (gesture == "Walk") gesture_type = 4;
                    // printf("gesture=%s\n", gesture.c_str());
                    
                    if (gesture_type > 0) {
                        shared_state_->imu.gesture = gesture_type;
                        shared_state_->imu.gesture_time_ms = SharedState::getCurrentTimeMs();
                        
                        if (gesture_type == 1 || gesture_type == 3) {
                            shared_state_->imu.shock_detected = true;
                        } else {
                            shared_state_->imu.shock_detected = false;
                        }
                    }
                }
            }
        } catch (const std::exception&) {
            // ignore parse errors
        }
    }
    
    zmq_close(socket);
    zmq_ctx_destroy(context);
    
    std::cout << LOG_PREFIX << " [SENSOR] IMU subscriber stopped" << std::endl;
}

void SensorSubscriber::power_subscriber_loop() {
    void* context = zmq_ctx_new();
    void* socket = zmq_socket(context, ZMQ_SUB);
    
    zmq_connect(socket, "ipc:///tmp/doly_sensor_pub.sock");
    
    const char* power_topic = "status.power";
    zmq_setsockopt(socket, ZMQ_SUBSCRIBE, power_topic, strlen(power_topic));
    
    int timeout = 1000;
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout, sizeof(timeout));
    
    std::cout << LOG_PREFIX << " [SENSOR] Power subscriber thread started" << std::endl;
    
    char topic_buf[256];
    char data_buf[1024];
    uint64_t power_count = 0;
    
    while (running_.load()) {
        int topic_size = zmq_recv(socket, topic_buf, sizeof(topic_buf) - 1, 0);
        if (topic_size < 0) continue;
        topic_buf[topic_size] = '\0';
        
        int data_size = zmq_recv(socket, data_buf, sizeof(data_buf) - 1, 0);
        if (data_size < 0) continue;
        data_buf[data_size] = '\0';
        
        // 采样率控制
        if (!power_limiter_.should_sample((uint64_t)SharedState::getCurrentTimeMs() * 1000)) {
            continue;
        }

        try {
            json data = json::parse(data_buf);
            if (data.contains("payload") && data["payload"].is_object()) {
                data = data["payload"];
            }
            
            power_count++;
            float raw_v = data.value("voltage", 0.0f);
            float raw_i = data.value("current", 0.0f);
            
            // 滤波处理
            float filt_v = power_voltage_filter_->update(raw_v);
            float filt_i = power_current_filter_->update(raw_i);
            
            if (config_.should_log(config_.power)) {
                std::cout << LOG_PREFIX << " ⚡ Power #" << power_count 
                          << ": V=" << filt_v << "V, I=" << filt_i << "A" << std::endl;
            }
            
            if (shared_state_) {
                shared_state_->power.voltage = filt_v;
                shared_state_->power.current = filt_i;
                shared_state_->power.percentage = data.value("percentage", (uint8_t)100);
                shared_state_->power.low_battery = (filt_v < 3.5f);
                shared_state_->power.update_time_ms = SharedState::getCurrentTimeMs();
            }
        } catch (const std::exception&) {
            // ignore
        }
    }
    
    zmq_close(socket);
    zmq_ctx_destroy(context);
    std::cout << LOG_PREFIX << " [SENSOR] Power subscriber stopped" << std::endl;
}

} // namespace doly::drive
