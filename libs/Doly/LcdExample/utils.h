// Utility helpers for LcdExample: types and image helpers
#ifndef NORAPY_LIB_LCD_UTILS_H
#define NORAPY_LIB_LCD_UTILS_H

#include <cstdint>
#include <png.h>

// Color union types used by conversion helpers
typedef union
{
    uint32_t RGB888;
    struct
    {
        uint32_t       :3;
        uint32_t RGB_B :5;
        uint32_t       :2;
        uint32_t RGB_G :6;
        uint32_t       :3;
        uint32_t RGB_R :5;
        uint32_t       :8;
    }Work;
}RGB888_struct;
typedef union
{
    uint16_t RGB565;
    struct
    {
        uint16_t RGB_R :5;
        uint16_t RGB_G :6;
        uint16_t RGB_B :5;
    }Work;
}RGB565_struct;

// Gamma table (defined in utils.cpp)
extern uint8_t gammaTable[256];

// Helpers
int16_t clamp(int16_t value, int16_t min, int16_t max);

uint16_t RGB888_To_RGB565(const RGB888_struct *RGB888);
uint32_t RGB565_To_RGB888(const RGB565_struct *RGB565);

// row_pointers: png rows (RGBA expected), output: packed 3-byte per pixel buffer
void applyDithering(png_bytep* row_pointers, int width, int height, uint8_t* output);

#endif // NORAPY_LIB_LCD_UTILS_H
