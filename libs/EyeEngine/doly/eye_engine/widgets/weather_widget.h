#pragma once

#include "widget_interface.h"
#include <atomic>
#include <chrono>
#include <limits>
#include <mutex>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

namespace doly {
namespace eye_engine {

// 天气图标类型
enum class WeatherIcon {
    Sunny,          // 晴天
    Cloudy,         // 多云
    Rainy,          // 雨天
    Snowy,          // 雪天
    Thunderstorm    // 雷暴
};

// 温度单位
enum class TempUnit {
    Celsius,    // 摄氏度
    Fahrenheit  // 华氏度
};

// Weather Widget 配置（优化版本）
struct WeatherStyle {
    // 颜色配置（RGB565）
    uint16_t temp_color = 0x07E0;           // 绿色：温度显示
    uint16_t icon_color = 0xFD80;           // 橙色：太阳、晴天
    uint16_t cloud_color = 0xFFFF;          // 白色：云
    uint16_t rain_color = 0x06FF;           // 青色：雨
    uint16_t background_color = 0x0000;     // 黑色：背景
    
    // 布局和显示
    std::string layout = "split";           // "split" 或 "single"
    bool show_temp_text = true;             // 显示温度数字
    bool show_icon = true;                  // 显示天气图标
    bool show_humidity = false;             // 显示湿度
};

// Weather Widget 实现
class WeatherWidget : public WidgetBase {
public:
    WeatherWidget();
    ~WeatherWidget() override = default;

    // IWidget 接口实现
    void update(double delta_time_ms) override;
    void render(FrameBuffer& buffer, const WidgetContext& ctx) override;
    ROI getUpdateROI() const override;
    bool needsRedraw() const override { return needs_redraw_; }
    bool updateConfig(const std::string& config_json) override;
    std::string getConfig() const override;
    void reset() override;

    // Weather 特定方法
    void setWeatherData(WeatherIcon icon,
                        int temp,
                        TempUnit unit = TempUnit::Celsius,
                        int humidity = -1,
                        std::string condition = {});
    void setStyle(const WeatherStyle& style);
    
    WeatherIcon getIcon() const { return current_icon_; }
    int getTemp() const { return temp_; }
    TempUnit getTempUnit() const { return temp_unit_; }
    int getHumidity() const { return humidity_; }

private:
    // 渲染辅助方法
    void renderSplit(FrameBuffer& buffer, const WidgetContext& ctx);
    void renderSingle(FrameBuffer& buffer, const WidgetContext& ctx);
    void drawIcon(FrameBuffer& buffer, int x, int y, int size);
    void drawSunIcon(FrameBuffer& buffer, int cx, int cy, int radius, uint16_t color);
    void drawCloudIcon(FrameBuffer& buffer, int cx, int cy, int size, uint16_t color);
    void drawRainIcon(FrameBuffer& buffer, int cx, int cy, int size, uint16_t color);
    void drawSnowIcon(FrameBuffer& buffer, int cx, int cy, int size, uint16_t color);
    void drawThunderstormIcon(FrameBuffer& buffer, int cx, int cy, int size, uint16_t color);
    void drawTemperatureDigit(FrameBuffer& buffer, int digit, int x, int y, int size, uint16_t color);
    void drawSegment(FrameBuffer& buffer, int x1, int y1, int x2, int y2, uint16_t color);
    void drawFilledCircle(FrameBuffer& buffer, int cx, int cy, int radius, uint16_t color);
    void drawCircle(FrameBuffer& buffer, int cx, int cy, int radius, uint16_t color);
    
    void applyStyleConfig(const nlohmann::json& style_json);
    
private:
    // 当前天气数据
    WeatherIcon current_icon_ = WeatherIcon::Sunny;
    int temp_ = 0;              // 温度值
    TempUnit temp_unit_ = TempUnit::Celsius;
    int humidity_ = -1;         // 湿度（-1表示不显示）
    
    // 上一次显示的数据（用于检测变化）
    WeatherIcon prev_icon_ = WeatherIcon::Sunny;
    int prev_temp_ = 0;
    int prev_humidity_ = -1;
    
    // 样式
    WeatherStyle style_;
    
    // Dirty 标记
    bool icon_dirty_ = true;
    bool temp_dirty_ = true;
    bool humidity_dirty_ = true;

    mutable std::mutex data_mutex_;
    std::atomic<bool> fetch_in_progress_{false};
};

} // namespace eye_engine
} // namespace doly
