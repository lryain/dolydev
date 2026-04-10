/**
 * @file graphics_primitives.h
 * @brief 基础图形绘制原语 (Bresenham 算法优化)
 * 
 * 提供高效的像素级绘制功能，用于 Widget 渲染。
 * 所有函数直接操作 RGB565 帧缓冲区。
 */

#ifndef DOLY_EYE_ENGINE_WIDGETS_GRAPHICS_PRIMITIVES_H_
#define DOLY_EYE_ENGINE_WIDGETS_GRAPHICS_PRIMITIVES_H_

#include <cstdint>
#include <algorithm>
#include <cmath>

namespace doly {
namespace eye_engine {
namespace widgets {

// RGB565 颜色常量
constexpr uint16_t COLOR_BLACK   = 0x0000;
constexpr uint16_t COLOR_WHITE   = 0xFFFF;
constexpr uint16_t COLOR_RED     = 0xF800;
constexpr uint16_t COLOR_GREEN   = 0x07E0;
constexpr uint16_t COLOR_BLUE    = 0x001F;
constexpr uint16_t COLOR_YELLOW  = 0xFFE0;
constexpr uint16_t COLOR_CYAN    = 0x07FF;
constexpr uint16_t COLOR_MAGENTA = 0xF81F;
constexpr uint16_t COLOR_ORANGE  = 0xFD20;
constexpr uint16_t COLOR_GRAY    = 0x8410;

/**
 * @brief RGB888 转 RGB565
 * @param r 红色分量 (0-255)
 * @param g 绿色分量 (0-255)
 * @param b 蓝色分量 (0-255)
 * @return RGB565 格式的颜色值
 */
inline uint16_t rgb888ToRgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

/**
 * @brief RGB565 转 RGB888
 * @param color RGB565 颜色
 * @param r 输出红色分量
 * @param g 输出绿色分量
 * @param b 输出蓝色分量
 */
inline void rgb565ToRgb888(uint16_t color, uint8_t& r, uint8_t& g, uint8_t& b) {
    r = (color >> 8) & 0xF8;
    g = (color >> 3) & 0xFC;
    b = (color << 3) & 0xF8;
}

/**
 * @brief 设置像素（带边界检查）
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x X 坐标
 * @param y Y 坐标
 * @param color RGB565 颜色
 */
inline void setPixel(uint16_t* buffer, int width, int height, 
                     int x, int y, uint16_t color) {
    if (x >= 0 && x < width && y >= 0 && y < height) {
        buffer[y * width + x] = color;
    }
}

/**
 * @brief 画直线 (Bresenham 算法)
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x0 起点 X
 * @param y0 起点 Y
 * @param x1 终点 X
 * @param y1 终点 Y
 * @param color RGB565 颜色
 */
void drawLine(uint16_t* buffer, int width, int height,
              int x0, int y0, int x1, int y1, uint16_t color);

/**
 * @brief 画水平线（优化版本）
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x 起点 X
 * @param y Y 坐标
 * @param len 线段长度
 * @param color RGB565 颜色
 */
void drawHLine(uint16_t* buffer, int width, int height,
               int x, int y, int len, uint16_t color);

/**
 * @brief 画垂直线（优化版本）
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x X 坐标
 * @param y 起点 Y
 * @param len 线段长度
 * @param color RGB565 颜色
 */
void drawVLine(uint16_t* buffer, int width, int height,
               int x, int y, int len, uint16_t color);

/**
 * @brief 画矩形（描边）
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x 左上角 X
 * @param y 左上角 Y
 * @param w 矩形宽度
 * @param h 矩形高度
 * @param color RGB565 颜色
 */
void drawRect(uint16_t* buffer, int width, int height,
              int x, int y, int w, int h, uint16_t color);

/**
 * @brief 填充矩形
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x 左上角 X
 * @param y 左上角 Y
 * @param w 矩形宽度
 * @param h 矩形高度
 * @param color RGB565 颜色
 */
void fillRect(uint16_t* buffer, int width, int height,
              int x, int y, int w, int h, uint16_t color);

/**
 * @brief 画圆（描边，Midpoint 算法）
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x0 圆心 X
 * @param y0 圆心 Y
 * @param radius 半径
 * @param color RGB565 颜色
 */
void drawCircle(uint16_t* buffer, int width, int height,
                int x0, int y0, int radius, uint16_t color);

/**
 * @brief 填充圆
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x0 圆心 X
 * @param y0 圆心 Y
 * @param radius 半径
 * @param color RGB565 颜色
 */
void fillCircle(uint16_t* buffer, int width, int height,
                int x0, int y0, int radius, uint16_t color);

/**
 * @brief 画圆角矩形（描边）
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x 左上角 X
 * @param y 左上角 Y
 * @param w 矩形宽度
 * @param h 矩形高度
 * @param radius 圆角半径
 * @param color RGB565 颜色
 */
void drawRoundRect(uint16_t* buffer, int width, int height,
                   int x, int y, int w, int h, int radius, uint16_t color);

/**
 * @brief 填充圆角矩形
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x 左上角 X
 * @param y 左上角 Y
 * @param w 矩形宽度
 * @param h 矩形高度
 * @param radius 圆角半径
 * @param color RGB565 颜色
 */
void fillRoundRect(uint16_t* buffer, int width, int height,
                   int x, int y, int w, int h, int radius, uint16_t color);

/**
 * @brief 画三角形（描边）
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x0,y0 第一个顶点
 * @param x1,y1 第二个顶点
 * @param x2,y2 第三个顶点
 * @param color RGB565 颜色
 */
void drawTriangle(uint16_t* buffer, int width, int height,
                  int x0, int y0, int x1, int y1, int x2, int y2, 
                  uint16_t color);

/**
 * @brief 填充三角形
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x0,y0 第一个顶点
 * @param x1,y1 第二个顶点
 * @param x2,y2 第三个顶点
 * @param color RGB565 颜色
 */
void fillTriangle(uint16_t* buffer, int width, int height,
                  int x0, int y0, int x1, int y1, int x2, int y2,
                  uint16_t color);

/**
 * @brief 根据 LCD 面积百分比计算字体缩放因子
 * 
 * 基于 Widget 元素占 LCD 总面积的百分比来计算缩放因子，
 * 实现线性、可预测的字体大小调整。
 * 
 * @param display_width LCD 宽度（像素）
 * @param display_height LCD 高度（像素）
 * @param area_percentage 占 LCD 面积的百分比 (1-100)，默认 100
 *        - 50: 占 LCD 面积的 50%
 *        - 100: 占 LCD 面积的 100%（最大）
 *        - 150: 占 LCD 面积的 150%（超出可能需要裁剪）
 * @return 缩放因子（整数）
 * 
 * 示例：
 *   LCD 240x240，area_percentage=100 → scale=2
 *   LCD 240x240，area_percentage=50  → scale=1
 *   LCD 240x240，area_percentage=150 → scale=3
 */
inline float computeScaleToFit(int display_width,
                               int display_height,
                               int content_width,
                               int content_height,
                               float scale_multiplier = 1.0f,
                               float min_scale = 0.5f,
                               float max_scale = 6.0f) {
    if (content_width <= 0 || content_height <= 0 || display_width <= 0 || display_height <= 0) {
        return 1.0f;
    }

    const float sx = static_cast<float>(display_width) / static_cast<float>(content_width);
    const float sy = static_cast<float>(display_height) / static_cast<float>(content_height);
    const float fit_scale = std::min(sx, sy);

    float scale = fit_scale * scale_multiplier;
    // 始终不超过可适配的最大值，避免溢出屏幕
    scale = std::min(scale, fit_scale);
    scale = std::clamp(scale, min_scale, max_scale);
    return scale;
}

}  // namespace widgets
}  // namespace eye_engine
}  // namespace doly

#endif  // DOLY_EYE_ENGINE_WIDGETS_GRAPHICS_PRIMITIVES_H_
