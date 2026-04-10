/**
 * @file font_renderer.h
 * @brief 位图字体渲染器（基于 efont.h 的 12x6 ASCII 字体）
 */

#ifndef DOLY_EYE_ENGINE_WIDGETS_FONT_RENDERER_H_
#define DOLY_EYE_ENGINE_WIDGETS_FONT_RENDERER_H_

#include <cstdint>
#include <string>

namespace doly {
namespace eye_engine {
namespace widgets {

/**
 * @brief 字体大小常量
 */
constexpr int FONT_WIDTH = 6;   // 每个字符宽度
constexpr int FONT_HEIGHT = 12;  // 每个字符高度

/**
 * @brief 文本对齐方式
 */
enum class TextAlign {
    LEFT,
    CENTER,
    RIGHT
};

/**
 * @brief 绘制单个字符（12x6 位图字体）
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x 字符左上角 X 坐标
 * @param y 字符左上角 Y 坐标
 * @param ch 字符（ASCII 32-126）
 * @param scale 缩放倍数（1=6x12, 2=12x24, etc.）
 * @param color RGB565 颜色
 */
void drawChar(uint16_t* buffer, int width, int height,
              int x, int y, char ch, int scale, uint16_t color);

/**
 * @brief 绘制字符串
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x 起始 X 坐标
 * @param y 起始 Y 坐标
 * @param text 文本内容（ASCII）
 * @param scale 缩放倍数
 * @param color RGB565 颜色
 * @return 绘制的总宽度（像素）
 */
int drawString(uint16_t* buffer, int width, int height,
               int x, int y, const char* text, int scale, uint16_t color);

/**
 * @brief 绘制字符串（带背景色）
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param x 起始 X 坐标
 * @param y 起始 Y 坐标
 * @param text 文本内容
 * @param scale 缩放倍数
 * @param fg_color 前景色 RGB565
 * @param bg_color 背景色 RGB565
 * @return 绘制的总宽度（像素）
 */
int drawStringWithBg(uint16_t* buffer, int width, int height,
                     int x, int y, const char* text, int scale,
                     uint16_t fg_color, uint16_t bg_color);

/**
 * @brief 计算字符串渲染宽度
 * @param text 文本内容
 * @param scale 缩放倍数
 * @return 宽度（像素）
 */
int measureString(const char* text, int scale);

/**
 * @brief 绘制居中对齐的字符串
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param center_x 中心 X 坐标
 * @param y 起始 Y 坐标
 * @param text 文本内容
 * @param scale 缩放倍数
 * @param color RGB565 颜色
 */
void drawStringCentered(uint16_t* buffer, int width, int height,
                        int center_x, int y, const char* text, int scale, uint16_t color);

/**
 * @brief 绘制右对齐的字符串
 * @param buffer 帧缓冲区
 * @param width 缓冲区宽度
 * @param height 缓冲区高度
 * @param right_x 右边界 X 坐标
 * @param y 起始 Y 坐标
 * @param text 文本内容
 * @param scale 缩放倍数
 * @param color RGB565 颜色
 */
void drawStringRight(uint16_t* buffer, int width, int height,
                     int right_x, int y, const char* text, int scale, uint16_t color);

}  // namespace widgets
}  // namespace eye_engine
}  // namespace doly

#endif  // DOLY_EYE_ENGINE_WIDGETS_FONT_RENDERER_H_
