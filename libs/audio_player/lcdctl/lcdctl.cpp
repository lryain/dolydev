#include "LcdControl.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void usage(){
    printf("Usage:\n");
    printf("  lcdctl init [12|18]\n");
    printf("  lcdctl fill L|R R G B\n");
    printf("  lcdctl write L|R <rgb24_file>\n");
    printf("  lcdctl bright <0-10>\n");
}

static int parseSide(const char* s){
    if(!s) return -1;
    if(s[0]=='L'||s[0]=='l') return LcdLeft;
    if(s[0]=='R'||s[0]=='r') return LcdRight;
    return -1;
}

int main(int argc, char** argv){
    if(argc < 2){ usage(); return 1; }
    std::string cmd = argv[1];

    if(cmd=="init"){
        LcdColorDepth depth = LCD_12BIT;
        if(argc>=3){ depth = (std::string(argv[2])=="18") ? LCD_18BIT : LCD_12BIT; }
        int8_t r = LcdControl::init(depth);
        if(r<0){ fprintf(stderr, "init failed: %d\n", r); return 2; }
        return 0;
    }

    if(cmd=="fill"){
        if(argc<6){ usage(); return 1; }
        int side = parseSide(argv[2]);
        if(side<0){ fprintf(stderr, "invalid side\n"); return 1; }
        int R=atoi(argv[3]), G=atoi(argv[4]), B=atoi(argv[5]);
        int8_t r0 = LcdControl::init(LCD_12BIT);
        (void)r0;
        LcdControl::LcdColorFill((LcdSide)side, (uint8_t)R,(uint8_t)G,(uint8_t)B);
        return 0;
    }

    if(cmd=="bright"){
        if(argc<3){ usage(); return 1; }
        int val = atoi(argv[2]);
        if(val<0) val=0; if(val>10) val=10;
        // int8_t r0 = LcdControl::init(LCD_18BIT);
        // (void)r0;
        int8_t r = LcdControl::setBrightness((uint8_t)val);
        if(r<0){ fprintf(stderr, "setBrightness failed: %d\n", r); return 3; }
        return 0;
    }

    if(cmd=="write"){
        if(argc<4){ usage(); return 1; }
        int side = parseSide(argv[2]);
        if(side<0){ fprintf(stderr, "invalid side\n"); return 1; }
        const char* path = argv[3];
        FILE* f = fopen(path, "rb");
        if(!f){ perror("fopen"); return 2; }
        // read file
        std::vector<uint8_t> rgb24;
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        if(sz<=0){ fclose(f); fprintf(stderr, "empty file\n"); return 2; }
        rgb24.resize(sz);
        if(fread(rgb24.data(),1,sz,f)!=(size_t)sz){ fclose(f); fprintf(stderr, "read failed\n"); return 2; }
        fclose(f);

        // 如果输入是 BMP 文件，解析 BMP 数据并提取为连续的 RGB24 像素数据
        if (sz > 54 && rgb24[0]=='B' && rgb24[1]=='M'){
            // BMP header fields are little-endian
            auto le32 = [&](int off)->uint32_t{
                return (uint32_t)rgb24[off] | ((uint32_t)rgb24[off+1]<<8) | ((uint32_t)rgb24[off+2]<<16) | ((uint32_t)rgb24[off+3]<<24);
            };
            auto le16 = [&](int off)->uint16_t{
                return (uint16_t)rgb24[off] | ((uint16_t)rgb24[off+1]<<8);
            };
            uint32_t pixelOffset = le32(10);
            int32_t bmpWidth = (int32_t)le32(18);
            int32_t bmpHeight = (int32_t)le32(22);
            uint16_t bpp = le16(28);
            if (bpp != 24){ fprintf(stderr, "Unsupported BMP bpp: %u\n", (unsigned)bpp); return 2; }
            int absH = bmpHeight>0? bmpHeight : -bmpHeight;
            int row_padded = (bmpWidth*3 + 3) & ~3;
            if ((uint32_t)pixelOffset + (uint32_t)row_padded * (uint32_t)absH > (uint32_t)sz){ fprintf(stderr, "BMP pixel data truncated\n"); return 2; }
            std::vector<uint8_t> rgbdata;
            rgbdata.resize((size_t)bmpWidth * (size_t)absH * 3);
            for(int y=0;y<absH;y++){
                int srcRow = (bmpHeight>0) ? (absH - 1 - y) : y; // bottom-up if height>0
                size_t srcOff = (size_t)pixelOffset + (size_t)srcRow * (size_t)row_padded;
                size_t dstOff = (size_t)y * (size_t)bmpWidth * 3;
                for(int x=0;x<bmpWidth;x++){
                    size_t s = srcOff + (size_t)x*3;
                    // BMP stores B G R
                    uint8_t B = rgb24[s+0];
                    uint8_t G = rgb24[s+1];
                    uint8_t R = rgb24[s+2];
                    rgbdata[dstOff + x*3 + 0] = R;
                    rgbdata[dstOff + x*3 + 1] = G;
                    rgbdata[dstOff + x*3 + 2] = B;
                }
            }
            // 如果 BMP 尺寸与期望 LCD 不符，尝试按 LcdControl.h 中的 LCD_WIDTH/LCD_HEIGHT 进行裁剪或报告错误
            if (bmpWidth != LCD_WIDTH || absH != LCD_HEIGHT){
                fprintf(stderr, "Warning: BMP size %dx%d differs from LCD %dx%d; content will be used as-is and may be cropped/scaled by panel driver if supported\n", bmpWidth, absH, LCD_WIDTH, LCD_HEIGHT);
            }
            // 替换 rgb24 为纯 RGB24 像素数据
            rgb24.swap(rgbdata);
            sz = rgb24.size();
        }

        // convert to panel depth
        int buf_sz = LcdControl::getBufferSize();
        std::vector<uint8_t> out(buf_sz);
        LcdControl::LcdBufferFrom24Bit(out.data(), rgb24.data());

        LcdData d; d.side=(uint8_t)side; d.buffer=out.data();
        int8_t r0 = LcdControl::init(LCD_12BIT);
        (void)r0;
        int8_t r = LcdControl::writeLcd(&d);
        if(r<0){ fprintf(stderr, "writeLcd failed: %d\n", r); return 3; }
        return 0;
    }

    usage();
    return 1;
}
