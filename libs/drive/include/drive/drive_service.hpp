/**
 * @file drive_service.hpp
 * @brief Drive 硬件服务主类
 * 
 * 封装：
 * - 共享内存初始化
 * - ZeroMQ 总线
 * - 硬件服务（PCA9535）
 * - 控制器（舵机、LED、电机）
 * - 传感器订阅
 * - 控制命令接收
 */

#pragma once

#include "drive/config_loader.hpp"
#include "drive/sensor_subscriber.hpp"
#include "doly/pca9535_service.hpp"
#include "doly/pca9535_bus_adapter.hpp"
#include "doly/pca9535_ovos_bridge.hpp"
#include "doly/zmq_control_receiver.hpp"
#include "doly/servo_controller.hpp"
#include "doly/led_controller.hpp"
#include "doly/zmq_publisher.hpp"
#include "drive/shared_state.hpp"
#include <memory>
#include <string>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <nlohmann/json.hpp>

class MotorController;

namespace doly::drive {

using json = nlohmann::json;

/**
 * @brief 命令类型枚举
 */
enum class CommandType {
    LED,
    SERVO,
    MOTOR,
    UNKNOWN
};

/**
 * @brief Drive 硬件服务
 */
class DriveService {
public:
    DriveService();
    ~DriveService();
    
    // 初始化并启动服务
    bool initialize(const std::string& config_file);
    
    // 停止并清理服务
    void shutdown();
    
    // 运行事件循环
    void run();
    
    // 获取共享状态（用于反射弧）
    SharedState* get_shared_state() { return shared_state_; }
    
    // 检查是否初始化成功
    bool is_initialized() const { return initialized_; }
    
private:
    bool init_shared_memory();
    bool init_zmq_bus();
    bool init_hardware_service(const doly::extio::Pca9535ConfigV2& config);
    bool init_controllers(const doly::extio::Pca9535ConfigV2& config);
    bool init_control_receiver();
    bool start_sensor_subscriber(const SensorLogConfig& config);
    
    bool cleanup();
    
    // 队列系统
    void start_command_executors();
    void stop_command_executors();
    CommandType classify_command(const json& cmd);
    void enqueue_command(const std::string& topic, const json& cmd);
    
    // 队列执行线程
    void led_executor_loop();
    void servo_executor_loop();
    void motor_executor_loop();
    
    // 命令执行函数
    void execute_led_command(const json& cmd);
    void execute_servo_command(const json& cmd);
    void execute_motor_command(const json& cmd);

    // 特殊命令处理
    bool handle_set_tof_address(const json& cmd);
    
    // 状态
    bool initialized_;
    
    // 共享内存
    SharedState* shared_state_;
    int shm_fd_;
    
    // 硬件服务
    std::shared_ptr<doly::extio::Pca9535Service> hw_service_;
    std::shared_ptr<doly::extio::Pca9535BusAdapter> bus_adapter_;
    
    // 控制器
    std::shared_ptr<doly::drive::ServoController> servo_ctrl_;
    std::shared_ptr<doly::drive::LedController> led_ctrl_;
    std::shared_ptr<MotorController> motor_ctrl_;
    
    // 控制命令
    std::unique_ptr<doly::extio::ZmqControlReceiver> control_receiver_;
    std::unique_ptr<doly::extio::Pca9535OvosBridge> ovos_bridge_;
    
    // 传感器
    std::unique_ptr<SensorSubscriber> sensor_subscriber_;

    std::shared_ptr<doly::ZmqPublisher> event_publisher_;

    bool publish_event(const std::string& topic, const std::string& payload);
    
    // 命令队列系统
    std::queue<json> led_command_queue_;
    std::queue<json> servo_command_queue_;
    std::queue<json> motor_command_queue_;
    
    // 队列互斥锁
    std::mutex led_queue_mutex_;
    std::mutex servo_queue_mutex_;
    std::mutex motor_queue_mutex_;
    
    // 队列条件变量
    std::condition_variable led_queue_cv_;
    std::condition_variable servo_queue_cv_;
    std::condition_variable motor_queue_cv_;
    
    // 执行线程
    std::thread led_executor_thread_;
    std::thread servo_executor_thread_;
    std::thread motor_executor_thread_;
    
    // 运行标志
    std::atomic<bool> executors_running_;
    
    // 队列容量限制
    static constexpr size_t MAX_QUEUE_SIZE = 50;
};

} // namespace doly::drive
