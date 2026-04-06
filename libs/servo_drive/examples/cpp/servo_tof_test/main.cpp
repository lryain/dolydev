#include "doly/pca9535_service.hpp"
#include "doly/pca9535_config_v2.hpp"
#include "doly/servo_controller.hpp"
#include <iostream>
#include <map>
#include <thread>
#include <chrono>

using namespace std::chrono_literals;

int main() {
    doly::extio::Pca9535ConfigV2 config; // defaults: all disabled
    doly::extio::Pca9535Service io;

    if (!io.init(&config)) {
        std::cerr << "[Example] Pca9535Service init failed" << std::endl;
        return 1;
    }

    // Test servo enable toggles
    std::cout << "[Example] Enabling both servos" << std::endl;
    io.enable_servo_both(true);
    std::this_thread::sleep_for(200ms);
    std::cout << "[Example] Disabling both servos" << std::endl;
    io.enable_servo_both(false);

    // TOF enable toggle
    // std::cout << "[Example] Enabling TOF" << std::endl;
    // io.enable_tof(true);
    // std::this_thread::sleep_for(100ms);
    // io.enable_tof(false);

    // Try TOF address configuration (may fail gracefully if hardware absent)
    std::cout << "[Example] Configuring TOF addresses (bus6 0x29/0x2A)" << std::endl;
    // io.configure_tof_addresses();

    // Servo controller basic move
    doly::drive::ServoController servo;
    servo.SetPowerCallback([&](ServoChannel ch, bool en) {
        if (ch == SERVO_LEFT) io.enable_servo_left(en);
        else if (ch == SERVO_RIGHT) io.enable_servo_right(en);
    });

    std::map<ServoChannel, float> initial{{SERVO_LEFT, 90.0f}, {SERVO_RIGHT, 90.0f}};
    std::map<ServoChannel, bool> autohold{{SERVO_LEFT, false}, {SERVO_RIGHT, false}};
    if (!servo.Init(initial, autohold)) {
        std::cerr << "[Example] ServoController init failed" << std::endl;
        return 1;
    }

    std::cout << "[Example] Move left servo to 60" << std::endl;
    servo.SetAngle(SERVO_LEFT, 60.0f, 60);
    std::this_thread::sleep_for(500ms);

    std::cout << "[Example] Move right servo to 120" << std::endl;
    servo.SetAngle(SERVO_RIGHT, 120.0f, 60);
    std::this_thread::sleep_for(500ms);

    servo.StopAll();
    io.enable_servo_both(false);
    std::cout << "[Example] Done" << std::endl;
    return 0;
}
