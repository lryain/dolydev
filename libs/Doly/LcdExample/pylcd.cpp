// 在文件开头添加

#include "LcdControl.h"
#include <string.h>
#include <stdio.h>
#include <thread>
#include <chrono>
#include <vector>
#include <cstdlib>
#include <png.h>
#include <zlib.h>
#include <cmath>

#include <dirent.h>

#include <iostream>
#include <filesystem>
#include <algorithm>


// helpers moved to utils.h / utils.cpp
#include "utils.h"

// Use AnimationFrame from animation_player.h
#include "animation_player.h"
// delegate image/frame loading to eye_frames.cpp
#include "eye_frames.h"



// Use AnimationPlayer to manage background playback and ownership
static AnimationPlayer* g_player = nullptr;

// Blocking player: write frames to both LCDs, do NOT free buffers (caller owns them)
static void PlayEyeAnimation(const std::vector<AnimationFrame>& frames) {
    for (const auto& frame : frames) {
        LcdData lcd_data;
        lcd_data.buffer = frame.buffer;
        lcd_data.side = LcdLeft;
        LcdControl::writeLcd(&lcd_data);
        lcd_data.side = LcdRight;
        LcdControl::writeLcd(&lcd_data);
        std::this_thread::sleep_for(std::chrono::milliseconds(frame.delay_ms));
    }
}

// forward declarations are in eye_frames.h; use those implementations

#ifdef __cplusplus
extern "C" {
#endif

extern "C" int lcd_main()
{
    // 初始化LCD
    LcdControl::init(LCD_18BIT);
    // for(int i=0; i<256; i++) {
    //     gammaTable[i] = static_cast<uint8_t>(round(pow(i/255.0, gamma_value) * 255));
    // }
    // 加载动画帧
    auto animation_frames = LoadEyeAnimationFrames();

    // 播放动画 (blocking)
    PlayEyeAnimation(animation_frames);

    // AnimationPlayer not used here; frames are owned locally so free them
    for(auto& frame : animation_frames) {
        delete[] frame.buffer;
    }

    return 0;
}

extern "C" int init_lcd()   {
    // 初始化LCD
    LcdControl::init(LCD_18BIT);
    return 1;
}

// 启动动画线程
// 启动动画线程（接收 frames 的所有权）
extern "C" int start_eye_animation(std::vector<AnimationFrame>& frames) {
    if (g_player) return -1; // already running
    g_player = AnimationPlayer::start(std::move(frames));
    return g_player ? 1 : -1;
}

// 停止动画线程
extern "C" int stop_eye_animation() {
    if (!g_player) return -1;
    g_player->stop();
    g_player = nullptr;
    return 1;
}

extern "C" int show_eye_test(int type, const char* input)   {
    LcdControl::init(LCD_18BIT);
    // 加载动画帧
    std::vector<AnimationFrame> animation_frames;

    printf("show_eye_test type: %d\n", type);

    if(type == 1){
        //const char* frame_files[] = {
        //                "/home/pi/eyes/35.png",
        //                "/home/pi/eyes/36.png",
        //                "/home/pi/eyes/37.png",
        //                "/home/pi/eyes/38.png",
        //                "/home/pi/eyes/39.png",
        //                // ... 其他png文件路径 ...
        //            };
        //animation_frames = LoadEyeAnimationFrames_test(frame_files, sizeof(frame_files)/sizeof(frame_files[0]), 3000);
    }else if(type == 2){
        //const char* frame_files[] = {
        //                        "/home/pi/eyes/e1.png",
        //                        "/home/pi/eyes/e2.png",
        //                        "/home/pi/eyes/e3.png",
        //                        "/home/pi/eyes/e4.png",
        //                        "/home/pi/eyes/e5.png",
        //                        "/home/pi/eyes/e6.png",
        //                        "/home/pi/eyes/e7.png"
        //                    };
        //animation_frames = LoadEyeAnimationFrames_test(frame_files, sizeof(frame_files)/sizeof(frame_files[0]), 3000);
    }else{
		std::string str(input);
		std::string folder_path = "/home/pi/eyes/" + str;
		printf("show_eye_test folder_path: %s \n", folder_path.c_str());
		auto png_files = getPngFiles(folder_path);
		//const char** frame_files = createFrameFilesArray(png_files);
		//printf("show_eye_test png_files : %d \n", png_files.size());

		animation_frames = LoadEyeAnimationFrames_test(input, png_files.size(), 30);

		// transfer ownership of frames to player (asynchronous)
		std::vector<AnimationFrame> frames_to_move = std::move(animation_frames);
		start_eye_animation(frames_to_move);

		//delete[] frame_files; // 使用后释放
	}

        // 如果没有在上面分支触发异步播放（type 1/2），则按原逻辑启动异步播放
        if (animation_frames.size() > 0) {
                std::vector<AnimationFrame> frames_to_move = std::move(animation_frames);
                start_eye_animation(frames_to_move);
        }

        return 1;
}

extern "C" int show_eye(char* frame_files[], int frame_delays[], int frame_count)   {
    // 加载动画帧
    std::vector<AnimationFrame> animation_frames = LoadEyeAnimationFrames(frame_files, frame_delays, frame_count);

    // 播放动画
    // blocking play
    PlayEyeAnimation(animation_frames);
    for(auto& frame : animation_frames) {
        delete[] frame.buffer;
    }
    return 0;
}

extern "C" int release_lcd(std::vector<AnimationFrame> animation_frames)   {
    // 清理资源
    for(auto& frame : animation_frames) {
        delete[] frame.buffer;
    }
    return 0;
}

// type 默认0，两只眼睛都显示，如果是1显示左边，如果是2显示右边
extern "C" int show_image(int type, unsigned char* frame_buffer)   {

    LcdData lcd_data;
    lcd_data.buffer = frame_buffer;

    if(type == 1){
        lcd_data.side = LcdLeft;
        LcdControl::writeLcd(&lcd_data);
    }else if (type == 2){
        lcd_data.side = LcdRight;
        LcdControl::writeLcd(&lcd_data);
    }else{
        lcd_data.side = LcdLeft;
        LcdControl::writeLcd(&lcd_data);

        lcd_data.side = LcdRight;
        LcdControl::writeLcd(&lcd_data);
    }
    return 1;
}

    // 加载动画帧
#ifdef __cplusplus
}
#endif