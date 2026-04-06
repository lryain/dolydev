/**
 * @example arm_test/main.cpp
 * @brief ArmControl API 测试
 * 
 * 测试内容:
 * - ArmControl 初始化/释放
 * - 事件监听器注册
 * - setAngle 运动命令
 * - 状态查询和角度查询
 */

#include <iostream>
#include <chrono>
#include <thread>

#include "ArmControl.h"
#include "ArmEvent.h"

// 事件回调
void onArmComplete(uint16_t id, ArmSide side) {
    std::cout << "[EVENT] Arm complete id=" << id << " side=" << (int)side << std::endl;
}

void onArmError(uint16_t id, ArmSide side, ArmErrorType errorType) {
    std::cout << "[EVENT] Arm error id=" << id << " side=" << (int)side
              << " type=" << (int)errorType << std::endl;
}

void onArmStateChange(ArmSide side, ArmState state) {
    const char* state_str = "?";
    switch (state) {
        case ArmState::RUNNING:   state_str = "RUNNING"; break;
        case ArmState::COMPLETED: state_str = "COMPLETED"; break;
        case ArmState::ERROR:     state_str = "ERROR"; break;
    }
    std::cout << "[EVENT] Arm state side=" << (int)side << " state=" << state_str << std::endl;
}

void onArmMovement(ArmSide side, float degreeChange) {
    // 仅在变化较大时打印（减少输出）
    if (std::abs(degreeChange) > 1.0f) {
        std::cout << "[EVENT] Arm movement side=" << (int)side
                  << " delta=" << degreeChange << std::endl;
    }
}

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "  ArmControl API Test" << std::endl;
    std::cout << "========================================" << std::endl;

    // 获取版本
    std::cout << "[INFO] ArmControl Version: " << ArmControl::getVersion() << std::endl;
    std::cout << "[INFO] Max Angle: " << ArmControl::getMaxAngle() << std::endl;

    // 注册事件监听器
    ArmEvent::AddListenerOnComplete(onArmComplete);
    ArmEvent::AddListenerOnError(onArmError);
    ArmEvent::AddListenerOnStateChange(onArmStateChange);
    ArmEvent::AddListenerOnMovement(onArmMovement);

    // 初始化
    std::cout << "\n--- Initializing ---" << std::endl;
    int8_t rc = ArmControl::init();
    if (rc < 0) {
        std::cerr << "[ERROR] ArmControl init failed: " << (int)rc << std::endl;
        return -1;
    }

    std::cout << "[INFO] isActive: " << (ArmControl::isActive() ? "true" : "false") << std::endl;

    // 等待初始化稳定
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 测试1: 双臂移动到 60 度
    std::cout << "\n--- Test 1: Move BOTH to 60° (speed=50) ---" << std::endl;
    ArmControl::setAngle(1, ArmSide::BOTH, 50, 60, false);

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

    // ========== 高级动作测试 ==========
    std::cout << "\n--- 高级动作: MoveMultiDuration ---" << std::endl;
    ArmControl::moveMultiDuration({{ArmSide::LEFT, 30}, {ArmSide::RIGHT, 150}}, 1200);
    std::this_thread::sleep_for(std::chrono::milliseconds(1500));

    std::cout << "\n--- 高级动作: ServoSwingOf (LEFT) ---" << std::endl;
    ArmControl::servoSwingOf(ArmSide::LEFT, 90, 60, 30, 50, 3);
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));

    std::cout << "\n--- 高级动作: StartSwing (RIGHT) ---" << std::endl;
    ArmControl::startSwing(ArmSide::RIGHT, 60, 120, 600, 4);
    std::this_thread::sleep_for(std::chrono::milliseconds(3000));

    std::cout << "\n--- 高级动作: liftDumbbell (LEFT, 40, 2) ---" << std::endl;
    ArmControl::liftDumbbell(ArmSide::LEFT, 40, 2);

    std::cout << "\n--- 高级动作: dumbbellDance (weight=30, 3s) ---" << std::endl;
    ArmControl::dumbbellDance(30, 3);

    std::cout << "\n--- 高级动作: waveFlag (RIGHT, 20, 4) ---" << std::endl;
    ArmControl::waveFlag(ArmSide::RIGHT, 20, 4);

    std::cout << "\n--- 高级动作: beatDrum (LEFT, 10, 3) ---" << std::endl;
    ArmControl::beatDrum(ArmSide::LEFT, 10, 3);

    std::cout << "\n--- 高级动作: paddleRow (RIGHT, 20, 2) ---" << std::endl;
    ArmControl::paddleRow(ArmSide::RIGHT, 20, 2);

    std::cout << "\n--- 高级动作: dualPaddleRow (weight=15, 2) ---" << std::endl;
    ArmControl::dualPaddleRow(15, 2);

    // 测试2: 左臂移动到 120 度
    std::cout << "\n--- Test 2: Move LEFT to 120° (speed=30) ---" << std::endl;
    ArmControl::setAngle(2, ArmSide::LEFT, 30, 120, false);

    while (ArmControl::getState(ArmSide::LEFT) == ArmState::RUNNING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));

    // 测试3: 回到 90 度 (使用 brake)
    std::cout << "\n--- Test 3: Move BOTH to 90° (speed=50, brake) ---" << std::endl;
    ArmControl::setAngle(3, ArmSide::BOTH, 50, 90, true);

    while (ArmControl::getState(ArmSide::BOTH) == ArmState::RUNNING) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    // 最终角度
    angles = ArmControl::getCurrentAngle(ArmSide::BOTH);
    for (const auto& a : angles) {
        std::cout << "[INFO] Final angle: side=" << (int)a.side
                  << " angle=" << a.angle << std::endl;
    }

    // 清理
    std::cout << "\n--- Cleanup ---" << std::endl;
    ArmEvent::RemoveListenerOnComplete(onArmComplete);
    ArmEvent::RemoveListenerOnError(onArmError);
    ArmEvent::RemoveListenerOnStateChange(onArmStateChange);
    ArmEvent::RemoveListenerOnMovement(onArmMovement);

    ArmControl::dispose();

    std::cout << "\n[INFO] Test completed!" << std::endl;
    return 0;
}
