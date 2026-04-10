#include "ArmControl.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    if (ArmControl::init() != 0) {
        std::cerr << "ArmControl init failed!" << std::endl;
        return 1;
    }
    std::cout << "ArmControl init success, ver=" << ArmControl::getVersion() << std::endl;
    
    // 高级动作测试对齐
    // std::cout << "\n--- SDK Test: MoveMultiDuration ---" << std::endl;
    // ArmControl::moveMultiDuration({{ArmSide::LEFT, 45}, {ArmSide::RIGHT, 135}}, 1000);
    // std::this_thread::sleep_for(std::chrono::milliseconds(6000));
    // std::cout << "\n--- SDK Test: waveFlag (BOTH, 30, 3) ---" << std::endl;
    // ArmControl::waveFlag(ArmSide::LEFT, 30, 10);
    // ArmControl::servoSwingOf(ArmSide::LEFT, 120, 60, 30, 60, 10);
    // std::this_thread::sleep_for(std::chrono::milliseconds(6000));
    // ArmControl::dumbbellDance(60, 5);
    // while (ArmControl::getState(ArmSide::BOTH) == ArmState::RUNNING) {
    //     std::this_thread::sleep_for(std::chrono::milliseconds(20));
    // }
    // std::cout << "\n--- SDK Test: dumbbellDance (weight=50, 2s) ---" << std::endl;
    // ArmControl::dumbbellDance(50, 2);
    // while (ArmControl::getState(ArmSide::BOTH) == ArmState::RUNNING) {
    //     std::this_thread::sleep_for(std::chrono::milliseconds(20));
    // }
    // ArmControl::setAngle(2, ArmSide::BOTH, 60, 180, false);
    // ArmControl::setAngle(1, ArmSide::LEFT, 60, 45, false);
    // ArmControl::setAngle(2, ArmSide::RIGHT, 60, 45, false);
    // while (ArmControl::getState(ArmSide::BOTH) == ArmState::RUNNING) {
    //     std::this_thread::sleep_for(std::chrono::milliseconds(20));
    // }
    // auto data = ArmControl::getCurrentAngle(ArmSide::BOTH);
    // for (const auto& item : data) {
    //     std::cout << "side=" << static_cast<int>(item.side) << " angle=" << item.angle << std::endl;
    // }

    // 测试1: 双臂移动到 60 度
    std::cout << "\n--- Test 1: Move BOTH to 60° (speed=50) ---" << std::endl;
    ArmControl::setAngle(1, ArmSide::BOTH, 50, 0, false);

    // 等待完成
    while (ArmControl::getState(ArmSide::BOTH) == ArmState::RUNNING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 查询角度
    auto angles = ArmControl::getCurrentAngle(ArmSide::BOTH);
    for (const auto& a : angles) {
        std::cout << "[INFO] Current angle: side=" << (int)a.side
                  << " angle=" << a.angle << std::endl;
    }

    ArmControl::dispose();
    std::cout << "Set both arms to 45 degrees." << std::endl;
    return 0;
}
