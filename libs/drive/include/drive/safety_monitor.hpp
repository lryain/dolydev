#ifndef SAFETY_MONITOR_HPP
#define SAFETY_MONITOR_HPP

#include <iostream>
#include <thread>
#include <atomic>
#include <functional>
#include <chrono>
#include <fstream>
#include <string>
#include <filesystem>

class SafetyMonitor {
private:
    std::string hwmon_path;  // sysfs path for INA219
    std::atomic<bool> monitoring;
    std::thread monitor_thread;

    // 安全阈值
    double max_current;      // 最大电流 (A)
    double max_voltage;      // 最大电压 (V)
    double min_voltage;      // 最小电压 (V)
    double max_temperature;  // 最大温度 (°C)

    // 当前值
    std::atomic<double> current;
    std::atomic<double> voltage;
    std::atomic<double> temperature;

    // 安全状态
    std::atomic<bool> overcurrent;
    std::atomic<bool> overvoltage;
    std::atomic<bool> undervoltage;
    std::atomic<bool> overtemperature;

    // 回调函数
    std::function<void(const std::string&)> safety_callback;

public:
    SafetyMonitor();
    ~SafetyMonitor();

    bool init();
    void start();
    void stop();

    // 配置安全阈值
    void setCurrentLimit(double max_current_a);
    void setVoltageLimits(double min_voltage_v, double max_voltage_v);
    void setTemperatureLimit(double max_temp_c);

    // 获取当前值
    double getCurrent() const { return current.load(); }
    double getVoltage() const { return voltage.load(); }
    double getTemperature() const { return temperature.load(); }

    // 检查安全状态
    bool isOvercurrent() const { return overcurrent.load(); }
    bool isOvervoltage() const { return overvoltage.load(); }
    bool isUndervoltage() const { return undervoltage.load(); }
    bool isOvertemperature() const { return overtemperature.load(); }
    bool isSafe() const;

    // 设置安全回调
    void setSafetyCallback(std::function<void(const std::string&)> callback);

private:
    void monitorLoop();
    bool readINA219(double& current, double& voltage);
    double readTemperature();  // 简单的CPU温度读取
    void checkSafetyConditions();
    std::string findINA219HwmonPath();
    int readSysfsValue(const std::string& filename);
};

#endif // SAFETY_MONITOR_HPP
