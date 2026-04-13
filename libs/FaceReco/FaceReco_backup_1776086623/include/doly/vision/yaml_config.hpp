#pragma once

#include <string>
#include <yaml-cpp/yaml.h>
#include "doly/vision/module_types.hpp"
#include "doly/vision/vision_bus_bridge.hpp"

namespace doly::vision {

/**
 * 统一的 YAML 配置加载器
 * 用于替代 Settings (INI) 系统
 */
class YAMLConfig {
public:
    static bool load(const std::string& yaml_path);
    static const YAML::Node& getRoot();
    
    // 便捷访问函数
    static std::string getString(const std::string& path, const std::string& default_value = "");
    static int getInt(const std::string& path, int default_value = 0);
    static double getDouble(const std::string& path, double default_value = 0.0);
    static float getFloat(const std::string& path, float default_value = 0.0f);
    static bool getBool(const std::string& path, bool default_value = false);
    
    // 专用配置加载
    static VisionBusBridge::Config loadBusConfig();
    static RunMode parseRunMode(const std::string& mode_str);
    
private:
    static YAML::Node root_;
    static YAML::Node getNodeByPath(const std::string& path);
};

} // namespace doly::vision
