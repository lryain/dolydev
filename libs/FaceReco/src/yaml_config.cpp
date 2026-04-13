#include "doly/vision/yaml_config.hpp"
#include <iostream>
#include <sstream>

namespace doly::vision {

YAML::Node YAMLConfig::root_;

bool YAMLConfig::load(const std::string& yaml_path) {
    try {
        root_ = YAML::LoadFile(yaml_path);
        std::cout << "[YAMLConfig] ✅ 配置加载成功: " << yaml_path << std::endl;
        return true;
    } catch (const YAML::Exception& e) {
        std::cerr << "[YAMLConfig] ❌ 加载失败: " << e.what() << std::endl;
        return false;
    }
}

const YAML::Node& YAMLConfig::getRoot() {
    return root_;
}

YAML::Node YAMLConfig::getNodeByPath(const std::string& path) {
    std::istringstream iss(path);
    std::string segment;
    YAML::Node current = root_;
    
    while (std::getline(iss, segment, '.')) {
        YAML::Node next = current[segment];
        if (!next || next.IsNull()) {
            return YAML::Node();
        }
        current = next;
    }
    
    return current.IsNull() ? YAML::Node() : current;
}

std::string YAMLConfig::getString(const std::string& path, const std::string& default_value) {
    YAML::Node node = getNodeByPath(path);
    if (!node || node.IsNull()) {
        return default_value;
    }
    
    try {
        return node.as<std::string>();
    } catch (const YAML::TypedBadConversion<std::string>& e) {
        // 尝试从其他类型转换
        try {
            if (node.IsScalar()) {
                // 使用 scalar 值
                return node.Scalar();
            }
        } catch (...) {}
        
        std::cerr << "[YAMLConfig] ⚠️ 字符串转换失败: " << path << ", 使用默认值: " << default_value << std::endl;
        return default_value;
    }
}

int YAMLConfig::getInt(const std::string& path, int default_value) {
    YAML::Node node = getNodeByPath(path);
    if (!node || node.IsNull()) {
        return default_value;
    }
    
    try {
        return node.as<int>();
    } catch (const YAML::TypedBadConversion<int>& e) {
        // 尝试从其他类型转换
        try {
            // 尝试 double
            double d = node.as<double>();
            return static_cast<int>(d);
        } catch (...) {}
        
        try {
            // 尝试字符串
            std::string str = node.as<std::string>();
            return std::stoi(str);
        } catch (...) {}
        
        std::cerr << "[YAMLConfig] ⚠️ 整数转换失败: " << path << ", 使用默认值: " << default_value << std::endl;
        return default_value;
    }
}

double YAMLConfig::getDouble(const std::string& path, double default_value) {
    YAML::Node node = getNodeByPath(path);
    if (!node || node.IsNull()) {
        return default_value;
    }
    
    try {
        return node.as<double>();
    } catch (const YAML::TypedBadConversion<double>& e) {
        // 尝试从其他类型转换
        try {
            int i = node.as<int>();
            return static_cast<double>(i);
        } catch (...) {}
        
        try {
            std::string str = node.as<std::string>();
            return std::stod(str);
        } catch (...) {}
        
        std::cerr << "[YAMLConfig] ⚠️ 浮点数转换失败: " << path << ", 使用默认值: " << default_value << std::endl;
        return default_value;
    }
}

float YAMLConfig::getFloat(const std::string& path, float default_value) {
    return static_cast<float>(getDouble(path, static_cast<double>(default_value)));
}

bool YAMLConfig::getBool(const std::string& path, bool default_value) {
    YAML::Node node = getNodeByPath(path);
    if (!node || node.IsNull()) {
        return default_value;
    }
    
    try {
        return node.as<bool>();
    } catch (const YAML::TypedBadConversion<bool>& e) {
        // 尝试从字符串转换
        try {
            std::string str = node.as<std::string>();
            if (str == "true" || str == "True" || str == "TRUE" || str == "1" || str == "yes") {
                return true;
            } else if (str == "false" || str == "False" || str == "FALSE" || str == "0" || str == "no") {
                return false;
            }
        } catch (...) {}
        
        std::cerr << "[YAMLConfig] ⚠️ 布尔值转换失败: " << path << ", 使用默认值: " << default_value << std::endl;
        return default_value;
    }
}

RunMode YAMLConfig::parseRunMode(const std::string& mode_str) {
    if (mode_str == "IDLE") return RunMode::IDLE;
    if (mode_str == "STREAM_ONLY") return RunMode::STREAM_ONLY;
    if (mode_str == "DETECT_ONLY") return RunMode::DETECT_ONLY;
    if (mode_str == "DETECT_TRACK") return RunMode::DETECT_TRACK;
    if (mode_str == "FULL") return RunMode::FULL;
    if (mode_str == "CUSTOM") return RunMode::CUSTOM;
    
    std::cerr << "[YAMLConfig] ⚠️ 未知模式: " << mode_str << ", 使用 IDLE" << std::endl;
    return RunMode::IDLE;
}

VisionBusBridge::Config YAMLConfig::loadBusConfig() {
    VisionBusBridge::Config config;
    
    // ZMQ 端点
    config.pub_endpoint = getString("zmq.pub_endpoint", "ipc:///tmp/doly_vision_events.sock");
    config.sub_endpoint = getString("zmq.sub_endpoint", "ipc:///tmp/doly_events.sock");
    config.source_id = getString("zmq.source_id", "vision_service");
    
    // 状态发布间隔
    config.status_interval_ms = getInt("service.status_interval_ms", 1000);
    config.auto_start = getBool("service.auto_start", true);
    config.mode_timeout_seconds = getInt("service.mode_timeout_seconds", 30);
    config.enable_mode_timeout = getBool("service.enable_mode_timeout", true);
    
    // 眼神跟随
    config.enable_eye_follow = getBool("gaze_follow.enabled", true);
    config.eye_follow_smoothing = getDouble("gaze_follow.smoothing", 0.12);
    config.gaze_publish_interval_ms = getInt("gaze_follow.publish_interval_ms", 50);
    
    // 初始运行模式
    std::string mode_str = getString("service.default_mode",
                                     getString("bus.initial_mode", "IDLE"));
    config.initial_mode = parseRunMode(mode_str);
    
    std::cout << "[YAMLConfig] 🔧 Bus配置加载完成:" << std::endl;
    std::cout << "  - pub_endpoint: " << config.pub_endpoint << std::endl;
    std::cout << "  - sub_endpoint: " << config.sub_endpoint << std::endl;
    std::cout << "  - initial_mode: " << mode_str << std::endl;
    std::cout << "  - auto_start: " << (config.auto_start ? "true" : "false") << std::endl;
    std::cout << "  - enable_eye_follow: " << (config.enable_eye_follow ? "true" : "false") << std::endl;
    std::cout << "  - mode_timeout: " << config.mode_timeout_seconds << "s ("
              << (config.enable_mode_timeout ? "on" : "off") << ")" << std::endl;
    
    return config;
}

} // namespace doly::vision
