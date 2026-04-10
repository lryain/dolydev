#include "ServoControl.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    if (ServoControl::init() != 0) {
        std::cerr << "ServoControl init failed!" << std::endl;
        return 1;
    }
    std::cout << "ServoControl init success, ver=" << ServoControl::getVersion() << std::endl;
    ServoControl::setServo(1, ServoId::SERVO_0, 90.0f, 100, true);
    ServoControl::setServo(2, ServoId::SERVO_1, 90.0f, 100, true);
    std::this_thread::sleep_for(std::chrono::seconds(1));
    ServoControl::release(ServoId::SERVO_0);
    ServoControl::release(ServoId::SERVO_1);
    ServoControl::dispose();
    std::cout << "Set both servos to 90 degrees." << std::endl;
    return 0;
}
