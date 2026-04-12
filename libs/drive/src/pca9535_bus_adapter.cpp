/**
 * @file pca9535_bus_adapter.cpp
 * @brief PCA9535 消息总线适配器实现 (ZeroMQ 版本 + 调试支持)
 

## 许可
GNU General Public License v3.0

## 作者
Kevin.Liu @ Make&Share
QQ: 47129927@qq.com

*/

#include "doly/pca9535_bus_adapter.hpp"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>
#include <cstring>

using json = nlohmann::json;

namespace doly {
namespace extio {

// 辅助函数：将 JSON 消息转换为字节数据
static inline std::vector<uint8_t> json_to_bytes(const nlohmann::json& json) {
    std::string str = json.dump();
    return std::vector<uint8_t>(str.begin(), str.end());
}

// 辅助函数：获取当前时间戳字符串（用于调试打印）
static inline std::string get_timestamp() {
    auto now = std::time(nullptr);
    auto tm = *std::localtime(&now);
    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S");
    return oss.str();
}

// 辅助函数：获取当前毫秒时间戳
static inline uint64_t get_timestamp_ms() {
    auto now = std::time(nullptr);
    return static_cast<uint64_t>(now) * 1000;
}

Pca9535BusAdapter::Pca9535BusAdapter(Pca9535Service& service, const Pca9535ConfigV2& config,
                     const std::shared_ptr<doly::ZmqPublisher>& publisher)
    : service_(service), config_(config), bus_publisher_(publisher) {
}

Pca9535BusAdapter::Pca9535BusAdapter(Pca9535Service& service,
                     const std::shared_ptr<doly::ZmqPublisher>& publisher)
    : service_(service), config_(Pca9535ConfigV2()), bus_publisher_(publisher) {
}

Pca9535BusAdapter::~Pca9535BusAdapter() {
    stop();
}

bool Pca9535BusAdapter::start() {
    if (running_.load()) {
        std::cerr << "[PCA9535 Bus Adapter] Already running" << std::endl;
        return false;
    }

    std::cout << "[PCA9535 Bus Adapter] Starting..." << std::endl;
    
    if (!bus_publisher_ || !bus_publisher_->is_ready()) {
        std::cerr << "[PCA9535 Bus Adapter] 公共 ZMQ 发布器未就绪，事件将无法发送" << std::endl;
    }

    if (config_.global_debug) {
        std::cout << "[DEBUG] 🔍 Global Debug Enabled - All events/commands will be logged" << std::endl;
    }

    // 订阅初始状态发布一次
    {
        uint16_t current_state = service_.get_state();
        auto now = std::chrono::high_resolution_clock::now();
        uint64_t ts_us = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()).count();
        on_raw_state({current_state, ts_us});
    }

    // 订阅原始状态
    subscription_ids_.push_back(
        service_.subscribe_raw_state([this](const RawStateEvent& event) {
            on_raw_state(event);
        })
    );

    // 订阅所有输入引脚的变化（6 个输入引脚）
    const std::vector<Pca9535Pin> input_pins = {
        Pca9535Pin::IRS_FL, Pca9535Pin::IRS_FR, Pca9535Pin::IRS_BL, Pca9535Pin::IRS_BR,
        Pca9535Pin::TOUCH_L, Pca9535Pin::TOUCH_R
    };

    for (auto pin : input_pins) {
        subscription_ids_.push_back(
            service_.subscribe_pin(pin, [this](const PinChangeEvent& event) {
                on_pin_change(event);
            })
        );
    }

    // 订阅触摸手势（左右）
    subscription_ids_.push_back(
        service_.subscribe_touch(Pca9535Pin::TOUCH_L, [this](const TouchGestureEvent& event) {
            on_touch_gesture(event);
        })
    );
    subscription_ids_.push_back(
        service_.subscribe_touch(Pca9535Pin::TOUCH_R, [this](const TouchGestureEvent& event) {
            on_touch_gesture(event);
        })
    );

    // 订阅触摸历史（左右）
    subscription_ids_.push_back(
        service_.subscribe_touch_history(Pca9535Pin::TOUCH_L, [this](const TouchHistoryEvent& event) {
            on_touch_history(event);
        })
    );
    subscription_ids_.push_back(
        service_.subscribe_touch_history(Pca9535Pin::TOUCH_R, [this](const TouchHistoryEvent& event) {
            on_touch_history(event);
        })
    );

    // 订阅悬崖模式（4 路）
    const std::vector<Pca9535Pin> cliff_pins = {
        Pca9535Pin::IRS_FL, Pca9535Pin::IRS_FR, Pca9535Pin::IRS_BL, Pca9535Pin::IRS_BR
    };

    for (auto pin : cliff_pins) {
        subscription_ids_.push_back(
            service_.subscribe_cliff(pin, [this](const CliffPatternEvent& event) {
                on_cliff_pattern(event);
            })
        );
        subscription_ids_.push_back(
            service_.subscribe_cliff_history(pin, [this](const CliffHistoryEvent& event) {
                on_cliff_history(event);
            })
        );
    }

    running_.store(true);
    std::cout << "[PCA9535 Bus Adapter] Started (" << subscription_ids_.size() << " subscriptions)" << std::endl;
    return true;
}

void Pca9535BusAdapter::stop() {
    if (!running_.load()) {
        return;
    }

    std::cout << "[PCA9535 Bus Adapter] Stopping..." << std::endl;

    // 取消所有订阅
    for (auto sub_id : subscription_ids_) {
        service_.unsubscribe(sub_id);
    }
    subscription_ids_.clear();

    running_.store(false);
    std::cout << "[PCA9535 Bus Adapter] Stopped" << std::endl;
}

void Pca9535BusAdapter::on_raw_state(const RawStateEvent& event) {
    // 检查是否启用该事件类型（决定是否进行事件处理）
    if (!config_.events.raw_state.enabled) {
        return;
    }
    
    // 检查是否应该打印调试信息
    bool debug = config_.should_debug(config_.events.raw_state.debug);
    
    // 格式化 state_hex
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(4) << std::setfill('0') << event.state;
    std::string state_hex = ss.str();

    // 获取 output_cache（输出状态）
    uint16_t output_cache = service_.get_output_cache();
    std::stringstream ss_cache;
    ss_cache << "0x" << std::hex << std::setw(4) << std::setfill('0') << output_cache;
    std::string output_cache_hex = ss_cache.str();

    json msg = {
        {"type", "raw_state"},
        {"state", event.state},
        {"state_hex", state_hex},
        {"output_cache", output_cache},
        {"output_cache_hex", output_cache_hex},
        {"ts_us", event.ts_us}
    };

    std::string topic = config_.full_topic(config_.events.raw_state.topic);
    
    if (debug) {
        std::cout << "[DEBUG-EVENT] 📊 RAW_STATE" << std::endl
                  << "  ├─ Topic: " << topic << std::endl
                  << "  ├─ State: " << state_hex << std::endl
                  << "  ├─ Output Cache: " << output_cache_hex << std::endl
                  << "  └─ Data: " << msg.dump() << std::endl;
    }
    
    // 检查是否应该发布事件到消息总线（emit_event 控制）
    if (!config_.events.raw_state.emit_event) {
        if (debug) {
            std::cout << "[DEBUG-SKIP] ⏭️  raw_state event publish disabled (emit_event=false)" << std::endl;
        }
        return;
    }
    
    // 创建字节数据并通过自定义发布器发布
    auto data = json_to_bytes(msg);
    publish_to_bus(topic, data, debug, "raw_state");
}

void Pca9535BusAdapter::on_pin_change(const PinChangeEvent& event) {
    // 检查是否启用该事件类型（决定是否进行事件处理）
    if (!config_.events.pin_change.enabled) {
        return;
    }
    
    // 检查是否应该打印调试信息
    bool debug = config_.should_debug(config_.events.pin_change.debug);
    
    // 获取 output_cache（输出状态）
    uint16_t output_cache = service_.get_output_cache();
    std::stringstream ss;
    ss << "0x" << std::hex << std::setw(4) << std::setfill('0') << output_cache;
    
    // 构造 JSON 消息
    json msg = {
        {"type", "pin_change"},
        {"pin", pin_to_string(event.pin)},
        {"value", event.value},
        {"output_cache", output_cache},
        {"output_cache_hex", ss.str()},
        {"ts_us", event.ts_us}
    };
    
    // 发布到配置的主题
    std::string topic = config_.full_topic(config_.events.pin_change.topic);
    auto data = json_to_bytes(msg);
    
    if (debug) {
        std::cout << "[DEBUG-EVENT] 📌 PIN_CHANGE" << std::endl
                  << "  ├─ Topic: " << topic << std::endl
                  << "  ├─ Pin: " << pin_to_string(event.pin) << std::endl
                  << "  ├─ Value: " << (event.value ? "HIGH" : "LOW") << std::endl
                  << "  └─ Data: " << msg.dump() << std::endl;
    }
    
    // 检查是否应该发布事件到消息总线（emit_event 控制）
    if (config_.events.pin_change.emit_event) {
        publish_to_bus(topic, data, debug, "pin_change");
    } else if (debug) {
        std::cout << "[DEBUG-SKIP] ⏭️  pin_change event publish disabled (emit_event=false)" << std::endl;
    }

}

void Pca9535BusAdapter::on_touch_gesture(const TouchGestureEvent& event) {
    // 检查是否启用该事件类型（决定是否进行事件处理）
    if (!config_.events.touch_gesture.enabled) {
        return;
    }
    
    // 检查是否应该打印调试信息
    bool debug = config_.should_debug(config_.events.touch_gesture.debug);
    
    std::string pin_name = pin_to_string(event.pin);
    std::string gesture_str = gesture_to_string(event.gesture);
    std::string side = (event.pin == Pca9535Pin::TOUCH_L) ? "left" : "right";

    json msg = {
        {"type", "touch_gesture"},
        {"pin", pin_name},
        {"side", side},
        {"gesture", gesture_str},
        {"duration_ms", event.duration_ms},
        {"ts_us", event.ts_us}
    };

    std::string topic = config_.full_topic(config_.events.touch_gesture.topic);
    auto data = json_to_bytes(msg);
    
    if (debug) {
        std::cout << "[DEBUG-EVENT] 🎵 TOUCH_GESTURE" << std::endl
                  << "  ├─ Topic: " << topic << std::endl
                  << "  ├─ Side: " << side << std::endl
                  << "  ├─ Gesture: " << gesture_str << std::endl
                  << "  ├─ Duration: " << event.duration_ms << "ms" << std::endl
                  << "  └─ Data: " << msg.dump() << std::endl;
    }
    
    // 检查是否应该发布事件到消息总线（emit_event 控制）
    if (config_.events.touch_gesture.emit_event) {
        publish_to_bus(topic, data, debug, "touch_gesture");
    } else if (debug) {
        std::cout << "[DEBUG-SKIP] ⏭️  touch_gesture event publish disabled (emit_event=false)" << std::endl;
    }
}

void Pca9535BusAdapter::on_touch_history(const TouchHistoryEvent& event) {
    // 检查是否启用该事件类型（决定是否进行事件处理）
    if (!config_.events.touch_history.enabled) {
        return;
    }
    
    // 检查是否应该打印调试信息
    bool debug = config_.should_debug(config_.events.touch_history.debug);
    
    std::string pin_name = pin_to_string(event.pin);
    std::string side = (event.pin == Pca9535Pin::TOUCH_L) ? "left" : "right";

    json msg = {
        {"type", "touch_history"},
        {"pin", pin_name},
        {"side", side},
        {"history", event.history},
        {"timestamps", event.timestamps},
        {"sample_count", event.history.size()},
        {"ts_us", event.ts_us}
    };

    std::string topic = config_.full_topic(config_.events.touch_history.topic);
    auto data = json_to_bytes(msg);
    
    if (debug) {
        std::cout << "[DEBUG-EVENT] 📜 TOUCH_HISTORY" << std::endl
                  << "  ├─ Topic: " << topic << std::endl
                  << "  ├─ Side: " << side << std::endl
                  << "  ├─ Sample Count: " << event.history.size() << std::endl
                  << "  └─ Data: " << msg.dump(2) << std::endl;
    }
    
    // 检查是否应该发布事件到消息总线（emit_event 控制）
    if (config_.events.touch_history.emit_event) {
        publish_to_bus(topic, data, debug, "touch_history");
    } else if (debug) {
        std::cout << "[DEBUG-SKIP] ⏭️  touch_history event publish disabled (emit_event=false)" << std::endl;
    }
}

void Pca9535BusAdapter::on_cliff_pattern(const CliffPatternEvent& event) {
    // 检查是否启用该事件类型（决定是否进行事件处理）
    if (!config_.events.cliff_pattern.enabled) {
        return;
    }
    
    // 检查是否应该打印调试信息
    bool debug = config_.should_debug(config_.events.cliff_pattern.debug);
    
    std::string pin_name = pin_to_string(event.pin);
    std::string pattern_str = pattern_to_string(event.pattern);
    
    // 提取位置（fl/fr/bl/br）
    std::string position;
    if (event.pin == Pca9535Pin::IRS_FL) position = "fl";
    else if (event.pin == Pca9535Pin::IRS_FR) position = "fr";
    else if (event.pin == Pca9535Pin::IRS_BL) position = "bl";
    else if (event.pin == Pca9535Pin::IRS_BR) position = "br";

    json msg = {
        {"type", "cliff_pattern"},
        {"pin", pin_name},
        {"position", position},
        {"pattern", pattern_str},
        {"ts_us", event.ts_us}
    };

    std::string topic = config_.full_topic(config_.events.cliff_pattern.topic);
    auto data = json_to_bytes(msg);
    
    if (debug) {
        std::cout << "[DEBUG-EVENT] 🏔️  CLIFF_PATTERN" << std::endl
                  << "  ├─ Topic: " << topic << std::endl
                  << "  ├─ Position: " << position << std::endl
                  << "  ├─ Pattern: " << pattern_str << std::endl
                  << "  └─ Data: " << msg.dump() << std::endl;
    }
    
    // 检查是否应该发布事件到消息总线（emit_event 控制）
    if (config_.events.cliff_pattern.emit_event) {
        publish_to_bus(topic, data, debug, "cliff_pattern");
    } else if (debug) {
        std::cout << "[DEBUG-SKIP] ⏭️  cliff_pattern event publish disabled (emit_event=false)" << std::endl;
    }
}

void Pca9535BusAdapter::on_cliff_history(const CliffHistoryEvent& event) {
    // 检查是否启用该事件类型（决定是否进行事件处理）
    if (!config_.events.cliff_history.enabled) {
        return;
    }
    
    // 检查是否应该打印调试信息
    bool debug = config_.should_debug(config_.events.cliff_history.debug);
    
    std::string pin_name = pin_to_string(event.pin);
    
    // 提取位置（fl/fr/bl/br）
    std::string position;
    if (event.pin == Pca9535Pin::IRS_FL) position = "fl";
    else if (event.pin == Pca9535Pin::IRS_FR) position = "fr";
    else if (event.pin == Pca9535Pin::IRS_BL) position = "bl";
    else if (event.pin == Pca9535Pin::IRS_BR) position = "br";

    json msg = {
        {"type", "cliff_history"},
        {"pin", pin_name},
        {"position", position},
        {"history", event.history},
        {"timestamps", event.timestamps},
        {"sample_count", event.history.size()},
        {"ts_us", event.ts_us}
    };

    std::string topic = config_.full_topic(config_.events.cliff_history.topic);
    auto data = json_to_bytes(msg);
    
    if (debug) {
        std::cout << "[DEBUG-EVENT] 📋 CLIFF_HISTORY" << std::endl
                  << "  ├─ Topic: " << topic << std::endl
                  << "  ├─ Position: " << position << std::endl
                  << "  ├─ Sample Count: " << event.history.size() << std::endl
                  << "  └─ Data: " << msg.dump(2) << std::endl;
    }
    
    // 检查是否应该发布事件到消息总线（emit_event 控制）
    if (config_.events.cliff_history.emit_event) {
        publish_to_bus(topic, data, debug, "cliff_history");
    } else if (debug) {
        std::cout << "[DEBUG-SKIP] ⏭️  cliff_history event publish disabled (emit_event=false)" << std::endl;
    }
}

bool Pca9535BusAdapter::publish_to_bus(const std::string& topic,
                                        const std::vector<uint8_t>& data,
                                        bool debug,
                                        const std::string& label) {
    if (!bus_publisher_ || !bus_publisher_->is_ready()) {
        if (debug) {
            std::cerr << "[DEBUG-ERROR] ZMQ publisher not ready for " << label << std::endl;
        }
        return false;
    }

    if (!bus_publisher_->publish(topic, data.data(), data.size())) {
        std::cerr << "[ERROR] ❌ Publish failed for " << label << " to topic: " << topic << std::endl;
        return false;
    }

    if (debug) {
        std::cout << "[DEBUG-PUBLISH] ✅ Published " << label << " successfully" << std::endl;
    }
    return true;
}

std::string Pca9535BusAdapter::pin_to_string(Pca9535Pin pin) const {
    switch (pin) {
        case Pca9535Pin::IRS_FL: return "IRS_FL";
        case Pca9535Pin::IRS_FR: return "IRS_FR";
        case Pca9535Pin::IRS_BL: return "IRS_BL";
        case Pca9535Pin::IRS_BR: return "IRS_BR";
        case Pca9535Pin::TOUCH_L: return "TOUCH_L";
        case Pca9535Pin::TOUCH_R: return "TOUCH_R";
        case Pca9535Pin::SRV_L_EN: return "SRV_L_EN";
        case Pca9535Pin::SRV_R_EN: return "SRV_R_EN";
        case Pca9535Pin::EXT_IO_0: return "EXT_IO_0";
        case Pca9535Pin::EXT_IO_1: return "EXT_IO_1";
        case Pca9535Pin::EXT_IO_2: return "EXT_IO_2";
        case Pca9535Pin::EXT_IO_3: return "EXT_IO_3";
        case Pca9535Pin::EXT_IO_4: return "EXT_IO_4";
        case Pca9535Pin::EXT_IO_5: return "EXT_IO_5";
        case Pca9535Pin::TOF_ENL: return "TOF_ENL";
        case Pca9535Pin::IRS_DRV: return "IRS_DRV";
        default: return "UNKNOWN";
    }
}

std::string Pca9535BusAdapter::gesture_to_string(TouchGesture gesture) const {
    switch (gesture) {
        case TouchGesture::SingleTap: return "SINGLE";
        case TouchGesture::DoubleTap: return "DOUBLE";
        case TouchGesture::LongPress: return "LONG_PRESS";
        default: return "UNKNOWN";
    }
}

std::string Pca9535BusAdapter::pattern_to_string(CliffPattern pattern) const {
    switch (pattern) {
        case CliffPattern::StableFloor: return "STABLE";
        case CliffPattern::BlackWhiteLine: return "LINE";
        case CliffPattern::CliffDetected: return "CLIFF";
        case CliffPattern::Noisy: return "NOISY";
        default: return "UNKNOWN";
    }
}

} // namespace extio
} // namespace doly
