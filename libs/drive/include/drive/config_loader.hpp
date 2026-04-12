/**
 * @file config_loader.hpp
 * @brief 配置文件加载器
 */

#pragma once

#include "drive/sensor_subscriber.hpp"
#include "doly/pca9535_config_v2.hpp"
#include <string>
#include <vector>

namespace doly::drive {

/**
 * @brief 配置加载器
 * 
 * 职责：
 * - 查找和加载 PCA9535 配置文件
 * - 加载传感器日志配置
 * - 打印配置诊断信息
 */
class ConfigLoader {
public:
    struct LoadResult {
        bool success;
        std::string config_file;
        doly::extio::Pca9535ConfigV2 config;
        SensorLogConfig sensor_log_config;
    };
    
    /**
     * @brief 加载配置
     * @param config_file 用户指定的配置文件路径
     * @return 加载结果
     */
    static LoadResult load(const std::string& config_file);
    
private:
    static std::vector<std::string> get_candidate_paths(const std::string& user_config);
    static void load_sensor_config(const std::string& yaml_file, SensorLogConfig& config);
    static void print_config_info(const LoadResult& result);
};

} // namespace doly::drive
