#include "LcdControl.h"
#include <iostream>
#include <thread>
#include <chrono>

int main() {
    if (LcdControl::init(LCD_12BIT) < 0) {
        std::cerr << "LcdControl init failed" << std::endl;
        return 1;
    }
    LcdControl::setBrightness(10);
    std::cout << "init ok, colorDepth=" << static_cast<int>(LcdControl::getColorDepth()) << std::endl;

    // fill left red
    LcdControl::LcdColorFill(LcdLeft, 255, 0, 0);
    std::this_thread::sleep_for(std::chrono::seconds(2));
    // fill right blue
    LcdControl::LcdColorFill(LcdRight, 0, 0, 255);
    std::this_thread::sleep_for(std::chrono::seconds(2));

    LcdControl::release();
    return 0;
}
