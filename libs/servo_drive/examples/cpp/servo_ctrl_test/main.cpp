/**
 * @example servo_ctrl_test/main.cpp
 * @brief ServoControl API 测试
 * 
 * 测试内容:
 * - ServoControl 初始化/释放
 * - 事件监听器注册
 * - setServo 运动命令
 * - release 释放
 */

#include <iostream>
#include <chrono>
#include <thread>

#include "ServoControl.h"
#include "ServoEvent.h"

void onServoComplete(uint16_t id, ServoId channel) {
    std::cout << "[EVENT] Servo complete id=" << id
              << " channel=" << (int)channel << std::endl;
}

void onServoAbort(uint16_t id, ServoId channel) {
    std::cout << "[EVENT] Servo abort id=" << id
              << " channel=" << (int)channel << std::endl;
}

void onServoError(uint16_t id, ServoId channel) {
    std::cout << "[EVENT] Servo error id=" << id
              << " channel=" << (int)channel << std::endl;
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  ServoControl API Test" << std::endl;
    std::cout << "========================================" << std::endl;

    // 获取版本
    std::cout << "[INFO] ServoControl Version: " << ServoControl::getVersion() << std::endl;

    // 注册事件
    ServoEvent::AddListenerOnComplete(onServoComplete);
    ServoEvent::AddListenerOnAbort(onServoAbort);
    ServoEvent::AddListenerOnError(onServoError);

    // 初始化
    std::cout << "\n--- Initializing ---" << std::endl;
    int8_t rc = ServoControl::init();
    if (rc < 0) {
        std::cerr << "[ERROR] ServoControl init failed: " << (int)rc << std::endl;
        return -1;
    }

    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 测试1: SERVO_0 移动到 60°
    std::cout << "\n--- Test 1: SERVO_0 to 60° ---" << std::endl;
    ServoControl::setServo(1, ServoId::SERVO_0, 60.0f, 50, false);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 测试2: SERVO_1 移动到 120°
    std::cout << "\n--- Test 2: SERVO_1 to 120° ---" << std::endl;
    ServoControl::setServo(2, ServoId::SERVO_1, 120.0f, 30, false);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 测试3: 两个舵机回到 90°
    std::cout << "\n--- Test 3: Both to 90° ---" << std::endl;
    ServoControl::setServo(3, ServoId::SERVO_0, 90.0f, 50, false);
    ServoControl::setServo(4, ServoId::SERVO_1, 90.0f, 50, false);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    // 释放
    std::cout << "\n--- Release ---" << std::endl;
    ServoControl::release(ServoId::SERVO_0);
    ServoControl::release(ServoId::SERVO_1);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 清理
    std::cout << "\n--- Cleanup ---" << std::endl;
    ServoEvent::RemoveListenerOnComplete(onServoComplete);
    ServoEvent::RemoveListenerOnAbort(onServoAbort);
    ServoEvent::RemoveListenerOnError(onServoError);

    ServoControl::dispose();

    std::cout << "\n[INFO] Test completed!" << std::endl;
    return 0;
}
