#pragma once

#include <string>
#include <map>
#include <vector>
#include <memory>
#include <fstream>
#include <sstream>

/**
 * @class ConfigManager
 * @brief 配置管理器，支持 INI 和 YAML 格式配置文件
 * 
 * 使用场景：
 * - 加载全局配置（分辨率、帧率、输入源等）
 * - 加载模块配置（启用/禁用模块、参数调整）
 * - 运行时更新配置（无需重启应用）
 * 
 * 配置文件格式 (INI):
 * [section_name]
 * key = value
 * 
 * 配置文件格式 (YAML):
 * section_name:
 *   key: value
 */
class ConfigManager {
public:
    // ==================== 构造和析构 ====================
    
    ConfigManager() = default;
    ~ConfigManager() = default;
    
    // 禁用拷贝和移动（单例模式推荐）
    ConfigManager(const ConfigManager&) = delete;
    ConfigManager& operator=(const ConfigManager&) = delete;

    // ==================== 配置加载 ====================

    /**
     * @brief 从 INI 文件加载配置
     * @param ini_file_path INI 文件路径
     * @return 加载是否成功
     */
    bool LoadFromINI(const std::string& ini_file_path);

    /**
     * @brief 从 YAML 文件加载配置（简化 YAML 支持）
     * @param yaml_file_path YAML 文件路径
     * @return 加载是否成功
     */
    bool LoadFromYAML(const std::string& yaml_file_path);

    // ==================== 配置读写 ====================

    /**
     * @brief 获取字符串值
     * @param section 配置节
     * @param key 键
     * @param default_value 默认值
     * @return 配置值
     */
    std::string GetString(const std::string& section, const std::string& key, 
                         const std::string& default_value = "") const;

    /**
     * @brief 获取整数值
     * @param section 配置节
     * @param key 键
     * @param default_value 默认值
     * @return 配置值
     */
    int GetInt(const std::string& section, const std::string& key, 
               int default_value = 0) const;

    /**
     * @brief 获取浮点数值
     * @param section 配置节
     * @param key 键
     * @param default_value 默认值
     * @return 配置值
     */
    float GetFloat(const std::string& section, const std::string& key, 
                   float default_value = 0.0f) const;

    /**
     * @brief 获取布尔值
     * @param section 配置节
     * @param key 键
     * @param default_value 默认值
     * @return 配置值
     */
    bool GetBool(const std::string& section, const std::string& key, 
                 bool default_value = false) const;

    /**
     * @brief 设置字符串值
     * @param section 配置节
     * @param key 键
     * @param value 值
     */
    void SetString(const std::string& section, const std::string& key, 
                   const std::string& value);

    /**
     * @brief 设置整数值
     * @param section 配置节
     * @param key 键
     * @param value 值
     */
    void SetInt(const std::string& section, const std::string& key, int value);

    /**
     * @brief 设置浮点数值
     * @param section 配置节
     * @param key 键
     * @param value 值
     */
    void SetFloat(const std::string& section, const std::string& key, float value);

    /**
     * @brief 设置布尔值
     * @param section 配置节
     * @param key 键
     * @param value 值
     */
    void SetBool(const std::string& section, const std::string& key, bool value);

    // ==================== 配置查询 ====================

    /**
     * @brief 检查某个键是否存在
     * @param section 配置节
     * @param key 键
     * @return 是否存在
     */
    bool HasKey(const std::string& section, const std::string& key) const;

    /**
     * @brief 检查某个节是否存在
     * @param section 配置节
     * @return 是否存在
     */
    bool HasSection(const std::string& section) const;

    /**
     * @brief 获取某个节下的所有键
     * @param section 配置节
     * @return 键列表
     */
    std::vector<std::string> GetKeys(const std::string& section) const;

    /**
     * @brief 获取所有节名
     * @return 节名列表
     */
    std::vector<std::string> GetSections() const;

    // ==================== 配置持久化 ====================

    /**
     * @brief 保存配置到 INI 文件
     * @param ini_file_path INI 文件路径
     * @return 保存是否成功
     */
    bool SaveToINI(const std::string& ini_file_path) const;

    /**
     * @brief 清除所有配置
     */
    void Clear();

    /**
     * @brief 获取配置摘要（用于调试）
     * @return 配置摘要字符串
     */
    std::string GetSummary() const;

private:
    // 内部数据结构：map<section, map<key, value>>
    std::map<std::string, std::map<std::string, std::string>> config_;

    // ==================== 辅助方法 ====================

    /**
     * @brief 字符串转整数
     */
    static int StringToInt(const std::string& str, int default_value);

    /**
     * @brief 字符串转浮点数
     */
    static float StringToFloat(const std::string& str, float default_value);

    /**
     * @brief 字符串转布尔值
     */
    static bool StringToBool(const std::string& str, bool default_value);

    /**
     * @brief 移除字符串前后的空白
     */
    static std::string Trim(const std::string& str);

    /**
     * @brief 解析 INI 行
     */
    static bool ParseINILine(const std::string& line, std::string& section, 
                            std::string& key, std::string& value);
};

/**
 * @typedef ConfigManagerPtr
 * @brief ConfigManager 智能指针
 */
using ConfigManagerPtr = std::shared_ptr<ConfigManager>;

/**
 * @brief 创建 ConfigManager 实例
 * @return 新的 ConfigManager
 */
inline ConfigManagerPtr CreateConfigManager() {
    return std::make_shared<ConfigManager>();
}
