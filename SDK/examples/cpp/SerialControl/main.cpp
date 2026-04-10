#include <serial/serial_service.hpp>
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    doly::serial::SerialService service;
    doly::serial::SerialConfig cfg;
    cfg.device = "/dev/ttyUSB0";
    cfg.baud = 115200;
    cfg.use_simulator = false;

    std::cout << "Initializing SerialControl..." << std::endl;
    if (!service.init(cfg)) {
        std::cerr << "Failed to init serial" << std::endl;
        return 1;
    }

    service.set_handler([](uint8_t byte) {
        std::cout << "Received byte: 0x" << std::hex << (int)byte << std::dec << std::endl;
    });

    std::cout << "Starting serial read loop (5 seconds test)..." << std::endl;
    service.start();

    std::this_thread::sleep_for(std::chrono::seconds(5));

    std::cout << "Stopping..." << std::endl;
    service.stop();
    return 0;
}
