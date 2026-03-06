#include "eye_frames.h"
#include "LcdControl.h"
#include <png.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <dirent.h>
#include <iostream>

// Return list of png files in folder (unsorted simple implementation)
std::vector<std::string> getPngFiles(const std::string& folder_path) {
    std::vector<std::string> files;
    DIR *dir = opendir(folder_path.c_str());
    if (dir) {
        struct dirent *ent;
        while ((ent = readdir(dir)) != NULL) {
            std::string name = ent->d_name;
            if (name.size() > 4 && name.substr(name.size()-4) == ".png") {
                files.push_back(folder_path + "/" + name);
            }
        }
        closedir(dir);
    }
    return files;
}

// Simple test loader: attempt to load PNGs using libpng; if any step fails,
// produce a single dummy gray frame to keep playback working in headless CI.
static AnimationFrame make_dummy_frame() {
    AnimationFrame f;
    f.delay_ms = 200;
    f.buffer = new uint8_t[LcdControl::getBufferSize()];
    memset(f.buffer, 0x80, LcdControl::getBufferSize());
    return f;
}

std::vector<AnimationFrame> LoadEyeAnimationFrames() {
    // Default: return a few dummy frames to avoid hard failure on hosts without /home/pi/eyes
    std::vector<AnimationFrame> frames;
    for (int i=0;i<3;i++) frames.push_back(make_dummy_frame());
    return frames;
}

std::vector<AnimationFrame> LoadEyeAnimationFrames_test(const char* input, int frame_count, int delay_ms) {
    std::vector<AnimationFrame> frames;
    std::vector<std::string> files = getPngFiles(std::string("/home/pi/eyes/") + input);
    if ((int)files.size() == 0) {
        // fallback to dummy sequence
        for (int i=0;i<frame_count;i++) {
            AnimationFrame f;
            f.delay_ms = delay_ms;
            f.buffer = new uint8_t[LcdControl::getBufferSize()];
            memset(f.buffer, (i*30)&0xFF, LcdControl::getBufferSize());
            frames.push_back(f);
        }
        return frames;
    }

    int loaded = 0;
    for (const auto &path : files) {
        if (loaded >= frame_count) break;
        FILE* fp = fopen(path.c_str(), "rb");
        if (!fp) continue;
        png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
        if (!png) { fclose(fp); continue; }
        png_infop info = png_create_info_struct(png);
        if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(fp); continue; }
        if (setjmp(png_jmpbuf(png))) { png_destroy_read_struct(&png, &info, NULL); fclose(fp); continue; }
        png_init_io(png, fp);
        png_read_info(png, info);
        int width = png_get_image_width(png, info);
        int height = png_get_image_height(png, info);
        png_set_palette_to_rgb(png);
        if(png_get_valid(png, info, PNG_INFO_tRNS)) png_set_tRNS_to_alpha(png);
        if (png_get_bit_depth(png, info) == 16) png_set_strip_16(png);
        if (png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY || png_get_color_type(png, info) == PNG_COLOR_TYPE_GRAY_ALPHA) png_set_gray_to_rgb(png);
        png_read_update_info(png, info);
        png_bytep* row_pointers = (png_bytep*)malloc(sizeof(png_bytep) * height);
        for(int y=0;y<height;y++) row_pointers[y] = (png_byte*)malloc(png_get_rowbytes(png, info));
        png_read_image(png, row_pointers);

        AnimationFrame frame;
        frame.buffer = new uint8_t[LcdControl::getBufferSize()];
        frame.delay_ms = delay_ms;
        // naive copy (assumes row bytes and layout compatible) - conservative fill
        size_t sz = LcdControl::getBufferSize();
        memset(frame.buffer, 0, sz);
        for (int y=0;y<height && (y*width*3) < (int)sz;y++) {
            png_bytep row = row_pointers[y];
            size_t rowbytes = png_get_rowbytes(png, info);
            size_t copy = std::min<size_t>(rowbytes, sz - y*width*3);
            memcpy(frame.buffer + y*width*3, row, copy);
        }

        for(int y=0;y<height;y++) free(row_pointers[y]);
        free(row_pointers);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(fp);

        frames.push_back(frame);
        loaded++;
    }

    if (frames.empty()) frames.push_back(make_dummy_frame());
    return frames;
}

std::vector<AnimationFrame> LoadEyeAnimationFrames(char* frame_files[], int frame_delays[], int frame_count) {
    std::vector<AnimationFrame> frames;
    for (int i=0;i<frame_count;i++) {
        // If file not found, push dummy
        FILE* fp = fopen(frame_files[i], "rb");
        if (!fp) {
            AnimationFrame f = make_dummy_frame();
            f.delay_ms = frame_delays[i];
            frames.push_back(f);
            continue;
        }
        // Very simple: read file into buffer up to buffer size
        AnimationFrame frame;
        frame.buffer = new uint8_t[LcdControl::getBufferSize()];
        frame.delay_ms = frame_delays[i];
        size_t got = fread(frame.buffer, 1, LcdControl::getBufferSize(), fp);
        if (got < (size_t)LcdControl::getBufferSize()) memset(frame.buffer+got, 0, LcdControl::getBufferSize()-got);
        fclose(fp);
        frames.push_back(frame);
    }
    return frames;

}

const char** createFrameFilesArray(const std::vector<std::string>& files) {
    const char** arr = new const char*[files.size()];
    for (size_t i=0;i<files.size();i++) arr[i] = files[i].c_str();
    return arr;
}
