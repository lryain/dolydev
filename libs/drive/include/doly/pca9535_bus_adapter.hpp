/**
 * @file pca9535_bus_adapter.hpp
 * @brief PCA9535 消息总线适配器
 * 
 * 将 PCA9535 事件发布到 Doly ZmqBus
 */

#pragma once

#include "pca9535_service.hpp"
#include "pca9535_config_v2.hpp"
#include "doly/zmq_publisher.hpp"
// #include "doly/high_speed_bus.hpp"
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <atomic>
#include <vector>

namespace doly {
namespace extio {

/**
 * @brief PCA9535 消息总线适配器
 * 
 * 功能：
 * 1. 订阅 PCA9535 所有事件
 * 2. 转换为 JSON 格式
 * 3. 发布到 ZmqBus
 */
class Pca9535BusAdapter {
public:
    /**
     * @brief 构造函数
     * @param service PCA9535 服务
     * @param config 配置对象
     */
    Pca9535BusAdapter(Pca9535Service& service,
                      const Pca9535ConfigV2& config,
                      const std::shared_ptr<doly::ZmqPublisher>& publisher);
    
    /**
     * @brief 构造函数（使用默认配置）
     * @param service PCA9535 服务
     */
    explicit Pca9535BusAdapter(Pca9535Service& service,
                               const std::shared_ptr<doly::ZmqPublisher>& publisher);

    /**
     * @brief 析构函数
     */
    ~Pca9535BusAdapter();

    /**
     * @brief 启动适配器（订阅所有事件）
     * @return 成功返回 true
     */
    bool start();

    /**
     * @brief 停止适配器（取消所有订阅）
     */
    void stop();

    /**
     * @brief 检查是否运行中
     */
    bool is_running() const { return running_.load(); }

private:
    /**
     * @brief 处理原始状态事件
     */
    void on_raw_state(const RawStateEvent& event);

    /**
     * @brief 处理引脚变化事件
     */
    void on_pin_change(const PinChangeEvent& event);

    /**
     * @brief 处理触摸手势事件
     */
    void on_touch_gesture(const TouchGestureEvent& event);

    /**
     * @brief 处理触摸历史事件
     */
    void on_touch_history(const TouchHistoryEvent& event);

    /**
     * @brief 处理悬崖模式事件
     */
    void on_cliff_pattern(const CliffPatternEvent& event);

    /**
     * @brief 处理悬崖历史事件
     */
    void on_cliff_history(const CliffHistoryEvent& event);

    /**
     * @brief 将引脚枚举转换为字符串
     */
    std::string pin_to_string(Pca9535Pin pin) const;

    /**
     * @brief 将触摸手势转换为字符串
     */
    std::string gesture_to_string(TouchGesture gesture) const;

    /**
     * @brief 将悬崖模式转换为字符串
     */
    std::string pattern_to_string(CliffPattern pattern) const;

    // 服务引用
    Pca9535Service& service_;
    
    // 配置
    Pca9535ConfigV2 config_;

    // 运行状态
    std::atomic<bool> running_{false};
    std::shared_ptr<doly::ZmqPublisher> bus_publisher_;

    // 订阅 ID 列表
    std::vector<uint64_t> subscription_ids_;

    bool publish_to_bus(const std::string& topic,
                        const std::vector<uint8_t>& data,
                        bool debug,
                        const std::string& label);
};

} // namespace extio
} // namespace doly
