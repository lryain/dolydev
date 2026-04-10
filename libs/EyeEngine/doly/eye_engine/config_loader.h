#pragma once

#include <string>
#include <memory>
#include <unordered_map>

namespace doly::eye_engine {

// Forward declaration
struct RuntimeControlValues;
struct EyeControlValues;

/**
 * @struct ParameterRange
 * @brief 参数范围定义（最小值、最大值、默认值）
 */
struct ParameterRange {
    double min;
    double max;
    double default_value;
    std::string description;
    
    ParameterRange() : min(0.0), max(1.0), default_value(0.5), description("") {}
    ParameterRange(double min_val, double max_val, double def_val, const std::string& desc = "")
        : min(min_val), max(max_val), default_value(def_val), description(desc) {}
    
    double clamp(double value) const {
        return (value < min) ? min : (value > max ? max : value);
    }
};

/**
 * @class ParameterRanges
 * @brief 参数范围管理器 - 从 JSON 配置加载并提供统一 clamp 接口
 */
class ParameterRanges {
public:
    ParameterRanges();
    
    /**
     * @brief 从 JSON 文件加载参数范围
     * @param json_path 参数范围配置文件路径
     * @return 是否加载成功
     */
    bool loadFromFile(const std::string& json_path);
    
    /**
     * @brief 获取参数范围
     * @param section 段名（如 "highlight", "pupil"）
     * @param param 参数名（如 "size", "alpha"）
     * @return 参数范围，如果未找到返回默认范围 [0, 1]
     */
    ParameterRange getRange(const std::string& section, const std::string& param) const;
    
    /**
     * @brief 限制参数到有效范围
     * @param section 段名
     * @param param 参数名
     * @param value 输入值
     * @return 限制后的值
     */
    double clamp(const std::string& section, const std::string& param, double value) const;
    
    /**
     * @brief 获取默认参数范围文件路径
     * @return 默认路径
     */
    static std::string getDefaultRangesPath();
    
private:
    // section -> (param -> range)
    std::unordered_map<std::string, std::unordered_map<std::string, ParameterRange>> ranges_;
    bool loaded_;
};

/**
 * @class ConfigLoader
 * @brief 眼睛引擎配置文件加载器
 * 
 * 负责从JSON配置文件读取默认的眼睛参数，支持：
 * - 全局运行时参数（背光、眨眼、凝视等）
 * - 左右眼的个性化参数（虹膜、瞳孔、眼睑等）
 * - 支持部分配置缺失，自动使用内置默认值作为回退
 */
class ConfigLoader {
public:
    ConfigLoader();
    ~ConfigLoader();

    /**
     * @brief 加载配置文件并初始化运行时参数
     * @param config_path 配置文件的完整路径（例如：/path/to/classic.json）
     * @param out_values 输出的RuntimeControlValues结构体
     * @param out_left_payload 可选：输出左眼的JSON payload（用于ParameterBus）
     * @param out_right_payload 可选：输出右眼的JSON payload（用于ParameterBus）
     * @return 是否成功加载（true=成功，false=失败但使用默认值）
     * 
     * @note 调用此方法后，可使用 out_left_payload 和 out_right_payload 直接传给
     *       ParameterBus::setProfile()，无需再调用 buildPayload()
     */
    bool loadRuntimeDefaults(const std::string& config_path, 
                            RuntimeControlValues& out_values,
                            std::string* out_left_payload = nullptr,
                            std::string* out_right_payload = nullptr);

    bool mergeRuntimeJson(const std::string& runtime_json,
                          RuntimeControlValues& out_values,
                          bool allow_scheduler_override = true);

    void mergeEyeJson(const std::string& eye_json, EyeControlValues& out_eye);

    /**
     * @brief 获取默认配置文件的路径
     * @return 默认配置文件路径，如果未找到返回空字符串
     */
    static std::string getDefaultConfigPath();

    /**
     * @brief 根据 RuntimeControlValues 生成 ParameterBus 所需的 JSON payload
     * @param values 运行时参数
     * @param eye_index 眼睛索引（左或右）
     * @return JSON格式的 payload 字符串
     * 
     * @note 这是从 ProceduralRuntimeEngine::buildPayload() 剥离出来的优化版本
     */
    static std::string buildPayload(const RuntimeControlValues& values, int eye_index);

private:
    /**
     * @brief 从JSON对象加载眼睛参数
     * @param eye_json JSON格式的眼睛对象
     * @param out_eye 输出的EyeControlValues结构体
     */
    void loadEyeValues(const std::string& eye_json, EyeControlValues& out_eye);

    /**
     * @brief 从JSON对象加载全局运行时参数
     * @param runtime_json JSON格式的运行时对象
     * @param out_values 输出的RuntimeControlValues结构体
     */
    void loadRuntimeValues(const std::string& runtime_json,
                           RuntimeControlValues& out_values,
                           bool allow_scheduler_override = true,
                           bool* out_scheduler_locked = nullptr);

    /**
     * @brief 初始化运行时参数为内置默认值
     * @param out_values 输出的RuntimeControlValues结构体
     */
    static void initializeDefaults(RuntimeControlValues& out_values);
};

}  // namespace doly::eye_engine
