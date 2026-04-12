/*

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "drive/safety_monitor.hpp"
#include <fstream>
#include <sstream>
#include <iostream>
#include <unistd.h>
#include <filesystem>

SafetyMonitor::SafetyMonitor()
    : monitoring(false),
      max_current(2.0), max_voltage(12.0), min_voltage(6.0), max_temperature(60.0),
      overcurrent(false), overvoltage(false), undervoltage(false), overtemperature(false) {
}

SafetyMonitor::~SafetyMonitor() {
    stop();
}

bool SafetyMonitor::init() {
    // Find INA219 hwmon path
    hwmon_path = findINA219HwmonPath();
    if (hwmon_path.empty()) {
        std::cerr << "INA219 hwmon device not found" << std::endl;
        return false;
    }
    std::cout << "Found INA219 at: " << hwmon_path << std::endl;
    return true;
}

void SafetyMonitor::start() {
    if (monitoring) return;
    monitoring = true;
    monitor_thread = std::thread(&SafetyMonitor::monitorLoop, this);
}

void SafetyMonitor::stop() {
    if (!monitoring) return;
    monitoring = false;
    if (monitor_thread.joinable()) {
        monitor_thread.join();
    }
}

void SafetyMonitor::monitorLoop() {
    while (monitoring) {
        checkSafetyConditions();
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // 100ms检查间隔
    }
}

void SafetyMonitor::checkSafetyConditions() {
    double current = 0.0, voltage = 0.0;
    bool sensor_ok = readINA219(current, voltage);
    double temperature = readTemperature();

    // 检查电流
    if (sensor_ok && current > max_current) {
        overcurrent = true;
        if (safety_callback) safety_callback("过流检测: " + std::to_string(current) + "A");
    } else {
        overcurrent = false;
    }

    // 检查电压
    if (sensor_ok) {
        if (voltage > max_voltage) {
            overvoltage = true;
            if (safety_callback) safety_callback("过压检测: " + std::to_string(voltage) + "V");
        } else {
            overvoltage = false;
        }

        if (voltage < min_voltage) {
            undervoltage = true;
            if (safety_callback) safety_callback("欠压检测: " + std::to_string(voltage) + "V");
        } else {
            undervoltage = false;
        }
    }

    // 检查温度
    if (temperature > max_temperature) {
        overtemperature = true;
        if (safety_callback) safety_callback("过温检测: " + std::to_string(temperature) + "°C");
    } else {
        overtemperature = false;
    }
}

bool SafetyMonitor::readINA219(double& current, double& voltage) {
    if (hwmon_path.empty()) return false;

    // Read voltage (in1_input in mV)
    int voltage_mv = readSysfsValue("in1_input");
    if (voltage_mv == -1) return false;
    voltage = voltage_mv / 1000.0;

    // Read current (curr1_input in uA)
    int current_ma = readSysfsValue("curr1_input");
    if (current_ma == -1) return false;
    current = current_ma / 1000000.0;

    return true;
}

double SafetyMonitor::readTemperature() {
    std::ifstream temp_file("/sys/class/thermal/thermal_zone0/temp");
    if (temp_file.is_open()) {
        std::string line;
        std::getline(temp_file, line);
        return std::stod(line) / 1000.0;
    }
    return 45.0; // 默认温度
}

bool SafetyMonitor::isSafe() const {
    return !overcurrent.load() && !overvoltage.load() &&
           !undervoltage.load() && !overtemperature.load();
}

void SafetyMonitor::setSafetyCallback(std::function<void(const std::string&)> callback) {
    safety_callback = callback;
}

void SafetyMonitor::setTemperatureLimit(double max_temp_c) {
    max_temperature = max_temp_c;
}

void SafetyMonitor::setCurrentLimit(double max_current_a) {
    max_current = max_current_a;
}

void SafetyMonitor::setVoltageLimits(double min_voltage_v, double max_voltage_v) {
    min_voltage = min_voltage_v;
    max_voltage = max_voltage_v;
}

std::string SafetyMonitor::findINA219HwmonPath() {
    const std::string base_path = "/sys/class/hwmon";
    try {
        for (const auto& entry : std::filesystem::directory_iterator(base_path)) {
            if (!entry.is_directory()) continue;
            
            std::string name_file = entry.path() / "name";
            if (std::filesystem::exists(name_file)) {
                std::ifstream name_stream(name_file);
                std::string name;
                std::getline(name_stream, name);
                if (name == "ina219") {
                    return entry.path().string();
                }
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Error finding INA219 hwmon: " << e.what() << std::endl;
    }
    return "";
}

int SafetyMonitor::readSysfsValue(const std::string& filename) {
    if (hwmon_path.empty()) return -1;
    
    std::ifstream file(hwmon_path + "/" + filename);
    if (!file.is_open()) return -1;
    
    std::string line;
    std::getline(file, line);
    try {
        return std::stoi(line);
    } catch (const std::exception&) {
        return -1;
    }
}
