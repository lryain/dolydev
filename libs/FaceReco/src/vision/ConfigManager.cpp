#include "vision/ConfigManager.h"
#include <algorithm>
#include <cctype>
#include <sstream>

// ==================== 配置加载 ====================

bool ConfigManager::LoadFromINI(const std::string& ini_file_path) {
    std::ifstream file(ini_file_path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    std::string current_section;

    while (std::getline(file, line)) {
        line = Trim(line);

        // 跳过空行和注释
        if (line.empty() || line[0] == ';' || line[0] == '#') {
            continue;
        }

        // 解析节头 [section]
        if (line[0] == '[' && line[line.length() - 1] == ']') {
            current_section = line.substr(1, line.length() - 2);
            current_section = Trim(current_section);
            continue;
        }

        // 解析键值对
        std::string section = current_section;
        std::string key, value;
        if (ParseINILine(line, section, key, value)) {
            config_[section][key] = value;
        }
    }

    file.close();
    return true;
}

bool ConfigManager::LoadFromYAML(const std::string& yaml_file_path) {
    // 简化的 YAML 支持（仅支持基本的 key: value 格式）
    std::ifstream file(yaml_file_path);
    if (!file.is_open()) {
        return false;
    }

    std::string line;
    std::string current_section;

    while (std::getline(file, line)) {
        // 检测缩进级别（在 trim 之前）
        int indent_level = 0;
        for (char c : line) {
            if (c == ' ') {
                indent_level++;
            } else if (c == '\t') {
                indent_level += 2;  // 制表符计为 2 个空格
            } else {
                break;
            }
        }

        // 移除缩进和尾部空白
        std::string trimmed = Trim(line);

        // 跳过空行和注释
        if (trimmed.empty() || trimmed[0] == '#') {
            continue;
        }

        // 根据缩进确定是节还是键值对
        if (indent_level == 0 || indent_level == 1) {  // 第一级：节
            // 这是一个节（冒号结尾，无值）
            if (trimmed.find(':') != std::string::npos) {
                size_t colon_pos = trimmed.find(':');
                std::string potential_section = trimmed.substr(0, colon_pos);
                potential_section = Trim(potential_section);
                
                // 检查冒号后是否有值
                std::string after_colon = trimmed.substr(colon_pos + 1);
                after_colon = Trim(after_colon);
                
                if (after_colon.empty()) {
                    // 这是节定义
                    current_section = potential_section;
                }
            }
        } else if (indent_level >= 2 && !current_section.empty()) {  // 第二级及以下：键值对
            // 这是键值对
            size_t colon_pos = trimmed.find(':');
            if (colon_pos != std::string::npos) {
                std::string key = trimmed.substr(0, colon_pos);
                std::string value = trimmed.substr(colon_pos + 1);
                key = Trim(key);
                value = Trim(value);
                config_[current_section][key] = value;
            }
        }
    }

    file.close();
    return true;
}

// ==================== 配置读写 ====================

std::string ConfigManager::GetString(const std::string& section, const std::string& key,
                                     const std::string& default_value) const {
    auto sec_it = config_.find(section);
    if (sec_it == config_.end()) {
        return default_value;
    }

    auto key_it = sec_it->second.find(key);
    if (key_it == sec_it->second.end()) {
        return default_value;
    }

    return key_it->second;
}

int ConfigManager::GetInt(const std::string& section, const std::string& key,
                         int default_value) const {
    std::string str = GetString(section, key);
    if (str.empty()) {
        return default_value;
    }
    return StringToInt(str, default_value);
}

float ConfigManager::GetFloat(const std::string& section, const std::string& key,
                             float default_value) const {
    std::string str = GetString(section, key);
    if (str.empty()) {
        return default_value;
    }
    return StringToFloat(str, default_value);
}

bool ConfigManager::GetBool(const std::string& section, const std::string& key,
                           bool default_value) const {
    std::string str = GetString(section, key);
    if (str.empty()) {
        return default_value;
    }
    return StringToBool(str, default_value);
}

void ConfigManager::SetString(const std::string& section, const std::string& key,
                             const std::string& value) {
    config_[section][key] = value;
}

void ConfigManager::SetInt(const std::string& section, const std::string& key, int value) {
    config_[section][key] = std::to_string(value);
}

void ConfigManager::SetFloat(const std::string& section, const std::string& key,
                            float value) {
    std::ostringstream oss;
    oss << value;
    config_[section][key] = oss.str();
}

void ConfigManager::SetBool(const std::string& section, const std::string& key, bool value) {
    config_[section][key] = value ? "true" : "false";
}

// ==================== 配置查询 ====================

bool ConfigManager::HasKey(const std::string& section, const std::string& key) const {
    auto sec_it = config_.find(section);
    if (sec_it == config_.end()) {
        return false;
    }
    return sec_it->second.find(key) != sec_it->second.end();
}

bool ConfigManager::HasSection(const std::string& section) const {
    return config_.find(section) != config_.end();
}

std::vector<std::string> ConfigManager::GetKeys(const std::string& section) const {
    std::vector<std::string> keys;
    auto sec_it = config_.find(section);
    if (sec_it != config_.end()) {
        for (const auto& kv : sec_it->second) {
            keys.push_back(kv.first);
        }
    }
    return keys;
}

std::vector<std::string> ConfigManager::GetSections() const {
    std::vector<std::string> sections;
    for (const auto& section_map : config_) {
        sections.push_back(section_map.first);
    }
    return sections;
}

// ==================== 配置持久化 ====================

bool ConfigManager::SaveToINI(const std::string& ini_file_path) const {
    std::ofstream file(ini_file_path);
    if (!file.is_open()) {
        return false;
    }

    for (const auto& section_map : config_) {
        file << "[" << section_map.first << "]\n";
        for (const auto& kv : section_map.second) {
            file << kv.first << " = " << kv.second << "\n";
        }
        file << "\n";
    }

    file.close();
    return true;
}

void ConfigManager::Clear() {
    config_.clear();
}

std::string ConfigManager::GetSummary() const {
    std::ostringstream oss;
    oss << "ConfigManager Summary:\n";
    for (const auto& section_map : config_) {
        oss << "  [" << section_map.first << "]\n";
        for (const auto& kv : section_map.second) {
            oss << "    " << kv.first << " = " << kv.second << "\n";
        }
    }
    return oss.str();
}

// ==================== 辅助方法 ====================

int ConfigManager::StringToInt(const std::string& str, int default_value) {
    try {
        return std::stoi(str);
    } catch (...) {
        return default_value;
    }
}

float ConfigManager::StringToFloat(const std::string& str, float default_value) {
    try {
        return std::stof(str);
    } catch (...) {
        return default_value;
    }
}

bool ConfigManager::StringToBool(const std::string& str, bool default_value) {
    std::string lower_str = str;
    std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(),
                   [](unsigned char c) { return std::tolower(c); });

    if (lower_str == "true" || lower_str == "yes" || lower_str == "1" ||
        lower_str == "on") {
        return true;
    } else if (lower_str == "false" || lower_str == "no" || lower_str == "0" ||
               lower_str == "off") {
        return false;
    }
    return default_value;
}

std::string ConfigManager::Trim(const std::string& str) {
    size_t start = 0;
    size_t end = str.length();

    while (start < end && std::isspace(str[start])) {
        start++;
    }

    while (end > start && std::isspace(str[end - 1])) {
        end--;
    }

    return str.substr(start, end - start);
}

bool ConfigManager::ParseINILine(const std::string& line, std::string& section,
                                std::string& key, std::string& value) {
    size_t equal_pos = line.find('=');
    if (equal_pos == std::string::npos) {
        return false;
    }

    key = line.substr(0, equal_pos);
    value = line.substr(equal_pos + 1);

    key = Trim(key);
    value = Trim(value);

    return true;
}
