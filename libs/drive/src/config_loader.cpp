/**
 * @file config_loader.cpp
 * @brief 配置加载器实现
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "drive/config_loader.hpp"
#include <yaml-cpp/yaml.h>
#include <iostream>
#include <unistd.h>

#define LOG_PREFIX "[ExtIOService]"

namespace doly::drive {

std::vector<std::string> ConfigLoader::get_candidate_paths(const std::string& user_config) {
    return {
        user_config,
        "/home/pi/dolydev/libs/drive/config/pca9535.yaml",
        // "/etc/doly/pca9535.yaml",
    };
}

void ConfigLoader::load_sensor_config(const std::string& yaml_file, SensorLogConfig& config) {
    try {
        YAML::Node yaml_config = YAML::LoadFile(yaml_file);
        if (!yaml_config["sensor_receiver"]) {
            return;
        }
        
        YAML::Node sr = yaml_config["sensor_receiver"];
        
        if (sr["tof"]) {
            config.tof.enabled = sr["tof"]["enabled"].as<bool>(true);
            config.tof.debug = sr["tof"]["debug"].as<bool>(true);
            config.tof.debug_raw = sr["tof"]["debug_raw"].as<bool>(false);
            config.tof.log_interval_ms = sr["tof"]["log_interval_ms"].as<uint64_t>(1000);
        }
        
        if (sr["ahrs"]) {
            config.ahrs.enabled = sr["ahrs"]["enabled"].as<bool>(true);
            config.ahrs.debug = sr["ahrs"]["debug"].as<bool>(true);
            config.ahrs.debug_raw = sr["ahrs"]["debug_raw"].as<bool>(false);
            config.ahrs.log_interval_ms = sr["ahrs"]["log_interval_ms"].as<uint64_t>(1000);
        }
        
        if (sr["gesture"]) {
            config.gesture.enabled = sr["gesture"]["enabled"].as<bool>(true);
            config.gesture.debug = sr["gesture"]["debug"].as<bool>(true);
            config.gesture.debug_raw = sr["gesture"]["debug_raw"].as<bool>(false);
            config.gesture.log_interval_ms = sr["gesture"]["log_interval_ms"].as<uint64_t>(0);
        }
        
        if (sr["power"]) {
            config.power.enabled = sr["power"]["enabled"].as<bool>(true);
            config.power.debug = sr["power"]["debug"].as<bool>(true);
            config.power.debug_raw = sr["power"]["debug_raw"].as<bool>(false);
            config.power.log_interval_ms = sr["power"]["log_interval_ms"].as<uint64_t>(2000);
        }
    } catch (const std::exception& e) {
        std::cerr << LOG_PREFIX << " ⚠️ Failed to load sensor_receiver config: " << e.what() << std::endl;
    }
}

void ConfigLoader::print_config_info(const LoadResult& result) {
    std::cout << LOG_PREFIX << " ✅ Config loaded from: " << result.config_file << std::endl;
    
    // 打印 PCA9535 硬件默认使能状态
    std::cout << LOG_PREFIX << "   - PCA9535 Hardware Defaults:" << std::endl;
    std::cout << LOG_PREFIX << "     * servo_left_default=" << (result.config.enable_servo_left_default ? "TRUE" : "FALSE") << std::endl;
    std::cout << LOG_PREFIX << "     * servo_right_default=" << (result.config.enable_servo_right_default ? "TRUE" : "FALSE") << std::endl;
    std::cout << LOG_PREFIX << "     * tof_default=" << (result.config.enable_tof_default ? "TRUE" : "FALSE") << std::endl;
    std::cout << LOG_PREFIX << "     * cliff_default=" << (result.config.enable_cliff_default ? "TRUE" : "FALSE") << std::endl;
    
    // 打印舵机配置（P1.3 新增）
    std::cout << LOG_PREFIX << "   - Servo Configuration (P1.3):" << std::endl;
    std::cout << LOG_PREFIX << "     * left: angle=" << result.config.servo.left.default_angle 
              << "° autohold=" << (result.config.servo.left.enable_autohold ? "Y" : "N")
              << " duration=" << result.config.servo.left.autohold_duration_ms << "ms" << std::endl;
    std::cout << LOG_PREFIX << "     * right: angle=" << result.config.servo.right.default_angle
              << "° autohold=" << (result.config.servo.right.enable_autohold ? "Y" : "N")
              << " duration=" << result.config.servo.right.autohold_duration_ms << "ms" << std::endl;
    
    // 打印传感器日志配置
    std::cout << LOG_PREFIX << "   - Sensor Receiver Config:" << std::endl;
    std::cout << LOG_PREFIX << "     * TOF: enabled=" << (result.sensor_log_config.tof.enabled ? "Y" : "N") 
              << " debug=" << (result.sensor_log_config.tof.debug ? "Y" : "N") << std::endl;
    std::cout << LOG_PREFIX << "     * AHRS: enabled=" << (result.sensor_log_config.ahrs.enabled ? "Y" : "N")
              << " debug=" << (result.sensor_log_config.ahrs.debug ? "Y" : "N") << std::endl;
    std::cout << LOG_PREFIX << "     * Gesture: enabled=" << (result.sensor_log_config.gesture.enabled ? "Y" : "N")
              << " debug=" << (result.sensor_log_config.gesture.debug ? "Y" : "N") << std::endl;
}

ConfigLoader::LoadResult ConfigLoader::load(const std::string& config_file) {
    LoadResult result{false, "", doly::extio::Pca9535ConfigV2(), SensorLogConfig()};
    
    std::cout << LOG_PREFIX << " [1/5] Loading configuration..." << std::endl;
    
    auto candidates = get_candidate_paths(config_file);
    
    for (const auto& path : candidates) {
        bool exists = (access(path.c_str(), F_OK) == 0);
        std::cout << LOG_PREFIX << " Checking: " << path << " -> " << (exists ? "FOUND" : "MISSING") << std::endl;
        
        if (exists) {
            try {
                result.config = doly::extio::Pca9535ConfigV2::from_yaml_file(path);
                result.config_file = path;
                result.success = true;
                
                load_sensor_config(path, result.sensor_log_config);
                print_config_info(result);
                
                return result;
            } catch (const std::exception& e) {
                std::cerr << LOG_PREFIX << " ⚠️ Failed to parse config at " << path << ": " << e.what() << std::endl;
            }
        }
    }
    
    std::cout << LOG_PREFIX << " ⚠️ Config file not found, using defaults" << std::endl;
    return result;
}

} // namespace doly::drive
