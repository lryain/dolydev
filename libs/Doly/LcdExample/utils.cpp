#include "utils.h"
#include <cmath>
#include <cstdlib>
#include <cstring>

uint8_t gammaTable[256];

int16_t clamp(int16_t value, int16_t min, int16_t max) {
    return (value < min) ? min : (value > max) ? max : value;
}

uint16_t RGB888_To_RGB565(const RGB888_struct *RGB888)
{
    RGB565_struct RGB565 = { 0 };

    RGB565.Work.RGB_R = RGB888->Work.RGB_R;
    RGB565.Work.RGB_G = RGB888->Work.RGB_G;
    RGB565.Work.RGB_B = RGB888->Work.RGB_B;
    return RGB565.RGB565;
}

uint32_t RGB565_To_RGB888(const RGB565_struct *RGB565)
{
    RGB888_struct RGB888 = { 0 };

    RGB888.Work.RGB_R = RGB565->Work.RGB_R;
    RGB888.Work.RGB_G = RGB565->Work.RGB_G;
    RGB888.Work.RGB_B = RGB565->Work.RGB_B;
    return RGB888.RGB888;
}

void applyDithering(png_bytep* row_pointers, int width, int height, uint8_t* output) {
    // Initialize gamma table with a common value if not set
    static bool gamma_initialized = false;
    if (!gamma_initialized) {
        for (int i = 0; i < 256; ++i) {
            gammaTable[i] = static_cast<uint8_t>(round(pow(i/255.0, 2.2) * 255));
        }
        gamma_initialized = true;
    }

    int16_t** errorR = new int16_t*[height];
    int16_t** errorG = new int16_t*[height];
    int16_t** errorB = new int16_t*[height];
    for(int y=0; y<height; y++) {
        errorR[y] = new int16_t[width]();
        errorG[y] = new int16_t[width]();
        errorB[y] = new int16_t[width]();
    }

    for(int y=0; y<height; y++) {
        png_bytep row = row_pointers[y];
        for(int x=0; x<width; x++) {
            png_bytep px = &(row[x*4]); // assume RGBA
            uint8_t r = gammaTable[px[0]];
            uint8_t g = gammaTable[px[1]];
            uint8_t b = gammaTable[px[2]];

            r = clamp(r + errorR[y][x], 0, 255);
            g = clamp(g + errorG[y][x], 0, 255);
            b = clamp(b + errorB[y][x], 0, 255);

            uint8_t r6 = r >> 2;
            uint8_t g6 = g >> 2;
            uint8_t b6 = b >> 2;

            int16_t errR = r - (r6 << 2);
            int16_t errG = g - (g6 << 2);
            int16_t errB = b - (b6 << 2);

            if(x+1 < width) {
                errorR[y][x+1] += errR * 7/16;
                errorG[y][x+1] += errG * 7/16;
                errorB[y][x+1] += errB * 7/16;
            }
            if(y+1 < height) {
                if(x > 0) {
                    errorR[y+1][x-1] += errR * 3/16;
                    errorG[y+1][x-1] += errG * 3/16;
                    errorB[y+1][x-1] += errB * 3/16;
                }
                errorR[y+1][x] += errR * 5/16;
                errorG[y+1][x] += errG * 5/16;
                errorB[y+1][x] += errB * 5/16;
                if(x+1 < width) {
                    errorR[y+1][x+1] += errR * 1/16;
                    errorG[y+1][x+1] += errG * 1/16;
                    errorB[y+1][x+1] += errB * 1/16;
                }
            }

            int byte_offset = (y * width + x) * 3;
            output[byte_offset] = r6;
            output[byte_offset+1] = g6;
            output[byte_offset+2] = b6;
        }
    }

    for(int y=0; y<height; y++) {
        delete[] errorR[y];
        delete[] errorG[y];
        delete[] errorB[y];
    }
    delete[] errorR;
    delete[] errorG;
    delete[] errorB;
}
