/**
 * @file pca9535_config_v2.cpp
 * @brief PCA9535 配置系统 v2.0 实现 - 支持 YAML/JSON 加载 + 调试开关
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/pca9535_config_v2.hpp"
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <iostream>

namespace doly {
namespace extio {

// 辅助函数：从 YAML 节点读取 TopicConfig
static TopicConfig load_topic_config(const YAML::Node& node, bool default_enabled = true) {
    TopicConfig config;
    if (!node) {
        config.enabled = default_enabled;
        config.emit_event = false;  // 默认不发布事件
        return config;
    }
    
    if (node["enabled"]) {
        config.enabled = node["enabled"].as<bool>();
    } else {
        config.enabled = default_enabled;
    }
    
    if (node["topic"]) {
        config.topic = node["topic"].as<std::string>();
    }
    
    if (node["emit_event"]) {
        config.emit_event = node["emit_event"].as<bool>();
    } else {
        config.emit_event = false;  // 默认不发布事件
    }
    
    if (node["debug"]) {
        config.debug = node["debug"].as<bool>();
    } else {
        config.debug = false;
    }
    
    return config;
}

// 辅助函数：从 JSON 节点读取 TopicConfig
static TopicConfig load_topic_config_json(const json& j, bool default_enabled = true) {
    TopicConfig config;
    
    if (j.contains("enabled")) {
        config.enabled = j.at("enabled").get<bool>();
    } else {
        config.enabled = default_enabled;
    }
    
    if (j.contains("topic")) {
        config.topic = j.at("topic").get<std::string>();
    }
    
    if (j.contains("emit_event")) {
        config.emit_event = j.at("emit_event").get<bool>();
    } else {
        config.emit_event = true;  // 默认发布事件
    }
    
    if (j.contains("debug")) {
        config.debug = j.at("debug").get<bool>();
    } else {
        config.debug = false;
    }
    
    return config;
}

Pca9535ConfigV2 Pca9535ConfigV2::from_json(const json& j) {
    Pca9535ConfigV2 config;
    
    // 全局配置
    if (j.contains("topic_prefix")) {
        config.topic_prefix = j.at("topic_prefix").get<std::string>();
    }
    
    if (j.contains("global_debug")) {
        config.global_debug = j.at("global_debug").get<bool>();
    }
    
    // 硬件默认状态
    if (j.contains("enable_servo_left_default")) {
        config.enable_servo_left_default = j.at("enable_servo_left_default").get<bool>();
    }
    if (j.contains("enable_servo_right_default")) {
        config.enable_servo_right_default = j.at("enable_servo_right_default").get<bool>();
    }
    if (j.contains("enable_tof_default")) {
        config.enable_tof_default = j.at("enable_tof_default").get<bool>();
    }
    if (j.contains("enable_cliff_default")) {
        config.enable_cliff_default = j.at("enable_cliff_default").get<bool>();
    }
    if (j.contains("auto_configure_tof")) {
        config.auto_configure_tof = j.at("auto_configure_tof").get<bool>();
    }
    
    // 事件配置
    if (j.contains("events")) {
        const auto& events = j.at("events");
        if (events.contains("raw_state")) {
            config.events.raw_state = load_topic_config_json(events.at("raw_state"), true);
        }
        if (events.contains("pin_change")) {
            config.events.pin_change = load_topic_config_json(events.at("pin_change"), true);
        }
        if (events.contains("touch_gesture")) {
            config.events.touch_gesture = load_topic_config_json(events.at("touch_gesture"), true);
        }
        if (events.contains("touch_history")) {
            config.events.touch_history = load_topic_config_json(events.at("touch_history"), true);
        }
        if (events.contains("cliff_pattern")) {
            config.events.cliff_pattern = load_topic_config_json(events.at("cliff_pattern"), true);
        }
        if (events.contains("cliff_history")) {
            config.events.cliff_history = load_topic_config_json(events.at("cliff_history"), true);
        }
    }
    
    // 命令配置
    if (j.contains("commands")) {
        const auto& commands = j.at("commands");
        if (commands.contains("enable_servo_left")) {
            config.commands.enable_servo_left = load_topic_config_json(commands.at("enable_servo_left"), true);
        }
        if (commands.contains("enable_servo_right")) {
            config.commands.enable_servo_right = load_topic_config_json(commands.at("enable_servo_right"), true);
        }
        if (commands.contains("enable_tof")) {
            config.commands.enable_tof = load_topic_config_json(commands.at("enable_tof"), true);
        }
        if (commands.contains("enable_cliff")) {
            config.commands.enable_cliff = load_topic_config_json(commands.at("enable_cliff"), true);
        }
        if (commands.contains("set_ext_io")) {
            config.commands.set_ext_io = load_topic_config_json(commands.at("set_ext_io"), true);
        }
    }
    
    // 触摸参数
    if (j.contains("touch")) {
        const auto& touch = j.at("touch");
        if (touch.contains("single_min_ms")) {
            config.touch.single_min_ms = touch.at("single_min_ms").get<uint32_t>();
        }
        if (touch.contains("single_max_ms")) {
            config.touch.single_max_ms = touch.at("single_max_ms").get<uint32_t>();
        }
        if (touch.contains("double_interval_ms")) {
            config.touch.double_interval_ms = touch.at("double_interval_ms").get<uint32_t>();
        }
        if (touch.contains("long_press_ms")) {
            config.touch.long_press_ms = touch.at("long_press_ms").get<uint32_t>();
        }
    }
    
    // 悬崖参数
    if (j.contains("cliff")) {
        const auto& cliff = j.at("cliff");
        if (cliff.contains("window_ms")) {
            config.cliff.window_ms = cliff.at("window_ms").get<uint32_t>();
        }
        if (cliff.contains("stable_max_edges")) {
            config.cliff.stable_max_edges = cliff.at("stable_max_edges").get<size_t>();
        }
        if (cliff.contains("line_min_edges")) {
            config.cliff.line_min_edges = cliff.at("line_min_edges").get<size_t>();
        }
        if (cliff.contains("cliff_duty_threshold")) {
            config.cliff.cliff_duty_threshold = cliff.at("cliff_duty_threshold").get<uint8_t>();
        }
    }
    
    // 舵机参数（P1.3 新增）
    if (j.contains("servo")) {
        const auto& servo = j.at("servo");
        
        if (servo.contains("left")) {
            const auto& left = servo.at("left");
            if (left.contains("default_angle")) {
                config.servo.left.default_angle = left.at("default_angle").get<uint16_t>();
            }
            if (left.contains("enable_autohold")) {
                config.servo.left.enable_autohold = left.at("enable_autohold").get<bool>();
            }
            if (left.contains("autohold_duration_ms")) {
                config.servo.left.autohold_duration_ms = left.at("autohold_duration_ms").get<uint32_t>();
            }
        }
        
        if (servo.contains("right")) {
            const auto& right = servo.at("right");
            if (right.contains("default_angle")) {
                config.servo.right.default_angle = right.at("default_angle").get<uint16_t>();
            }
            if (right.contains("enable_autohold")) {
                config.servo.right.enable_autohold = right.at("enable_autohold").get<bool>();
            }
            if (right.contains("autohold_duration_ms")) {
                config.servo.right.autohold_duration_ms = right.at("autohold_duration_ms").get<uint32_t>();
            }
        }
    }
    
    return config;
}

Pca9535ConfigV2 Pca9535ConfigV2::from_yaml_file(const std::string& filepath) {
    try {
        YAML::Node yaml = YAML::LoadFile(filepath);
        
        Pca9535ConfigV2 config;
        
        // 全局配置
        if (yaml["topic_prefix"]) {
            config.topic_prefix = yaml["topic_prefix"].as<std::string>();
        }
        
        if (yaml["global_debug"]) {
            config.global_debug = yaml["global_debug"].as<bool>();
        }
        
        // 硬件默认状态
        if (yaml["enable_servo_left_default"]) {
            config.enable_servo_left_default = yaml["enable_servo_left_default"].as<bool>();
        }
        if (yaml["enable_servo_right_default"]) {
            config.enable_servo_right_default = yaml["enable_servo_right_default"].as<bool>();
        }
        if (yaml["enable_tof_default"]) {
            config.enable_tof_default = yaml["enable_tof_default"].as<bool>();
        }
        if (yaml["enable_cliff_default"]) {
            config.enable_cliff_default = yaml["enable_cliff_default"].as<bool>();
        }
        
        // hardware_defaults 块（新版配置格式）
        if (yaml["hardware_defaults"]) {
            YAML::Node hw = yaml["hardware_defaults"];
            if (hw["enable_servo_left"]) {
                config.enable_servo_left_default = hw["enable_servo_left"].as<bool>();
            }
            if (hw["enable_servo_right"]) {
                config.enable_servo_right_default = hw["enable_servo_right"].as<bool>();
            }
            if (hw["enable_tof"]) {
                config.enable_tof_default = hw["enable_tof"].as<bool>();
            }
            if (hw["enable_cliff"]) {
                config.enable_cliff_default = hw["enable_cliff"].as<bool>();
            }
            if (hw["auto_configure_tof"]) {
                config.auto_configure_tof = hw["auto_configure_tof"].as<bool>();
            }
        }
        
        // 事件配置
        if (yaml["events"]) {
            YAML::Node events = yaml["events"];
            
            if (events["raw_state"]) {
                config.events.raw_state = load_topic_config(events["raw_state"], true);
            }
            if (events["pin_change"]) {
                config.events.pin_change = load_topic_config(events["pin_change"], true);
            }
            if (events["touch_gesture"]) {
                config.events.touch_gesture = load_topic_config(events["touch_gesture"], true);
            }
            if (events["touch_history"]) {
                config.events.touch_history = load_topic_config(events["touch_history"], true);
            }
            if (events["cliff_pattern"]) {
                config.events.cliff_pattern = load_topic_config(events["cliff_pattern"], true);
            }
            if (events["cliff_history"]) {
                config.events.cliff_history = load_topic_config(events["cliff_history"], true);
            }
        }
        
        // 命令配置
        if (yaml["commands"]) {
            YAML::Node commands = yaml["commands"];
            
            if (commands["enable_servo_left"]) {
                config.commands.enable_servo_left = load_topic_config(commands["enable_servo_left"], true);
            }
            if (commands["enable_servo_right"]) {
                config.commands.enable_servo_right = load_topic_config(commands["enable_servo_right"], true);
            }
            if (commands["enable_tof"]) {
                config.commands.enable_tof = load_topic_config(commands["enable_tof"], true);
            }
            if (commands["enable_cliff"]) {
                config.commands.enable_cliff = load_topic_config(commands["enable_cliff"], true);
            }
            if (commands["set_ext_io"]) {
                config.commands.set_ext_io = load_topic_config(commands["set_ext_io"], true);
            }
        }
        
        // 触摸参数
        if (yaml["touch"]) {
            YAML::Node touch = yaml["touch"];
            
            if (touch["single_min_ms"]) {
                config.touch.single_min_ms = touch["single_min_ms"].as<uint32_t>();
            }
            if (touch["single_max_ms"]) {
                config.touch.single_max_ms = touch["single_max_ms"].as<uint32_t>();
            }
            if (touch["double_interval_ms"]) {
                config.touch.double_interval_ms = touch["double_interval_ms"].as<uint32_t>();
            }
            if (touch["long_press_ms"]) {
                config.touch.long_press_ms = touch["long_press_ms"].as<uint32_t>();
            }
        }
        
        // 悬崖参数
        if (yaml["cliff"]) {
            YAML::Node cliff = yaml["cliff"];
            
            if (cliff["window_ms"]) {
                config.cliff.window_ms = cliff["window_ms"].as<uint32_t>();
            }
            if (cliff["stable_max_edges"]) {
                config.cliff.stable_max_edges = cliff["stable_max_edges"].as<size_t>();
            }
            if (cliff["line_min_edges"]) {
                config.cliff.line_min_edges = cliff["line_min_edges"].as<size_t>();
            }
            if (cliff["cliff_duty_threshold"]) {
                int temp = cliff["cliff_duty_threshold"].as<int>();
                config.cliff.cliff_duty_threshold = static_cast<uint8_t>(temp);
            }
        }
        
        // 舵机参数（P1.3 新增）
        if (yaml["servo"]) {
            YAML::Node servo = yaml["servo"];
            
            if (servo["left"]) {
                YAML::Node left = servo["left"];
                if (left["default_angle"]) {
                    config.servo.left.default_angle = left["default_angle"].as<uint16_t>();
                }
                if (left["enable_autohold"]) {
                    config.servo.left.enable_autohold = left["enable_autohold"].as<bool>();
                }
                if (left["autohold_duration_ms"]) {
                    config.servo.left.autohold_duration_ms = left["autohold_duration_ms"].as<uint32_t>();
                }
            }
            
            if (servo["right"]) {
                YAML::Node right = servo["right"];
                if (right["default_angle"]) {
                    config.servo.right.default_angle = right["default_angle"].as<uint16_t>();
                }
                if (right["enable_autohold"]) {
                    config.servo.right.enable_autohold = right["enable_autohold"].as<bool>();
                }
                if (right["autohold_duration_ms"]) {
                    config.servo.right.autohold_duration_ms = right["autohold_duration_ms"].as<uint32_t>();
                }
            }
        }
        
        std::cout << "[Config] ✅ Loaded from YAML: " << filepath << std::endl;
        if (config.global_debug) {
            std::cout << "[Config] 🔍 Global Debug Enabled!" << std::endl;
        }
        
        return config;
    } catch (const YAML::Exception& e) {
        std::cerr << "[Config] ❌ YAML parsing error: " << e.what() << std::endl;
        throw;
    } catch (const std::exception& e) {
        std::cerr << "[Config] ❌ Error: " << e.what() << std::endl;
        throw;
    }
}

} // namespace extio
} // namespace doly
