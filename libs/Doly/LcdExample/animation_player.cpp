#include "animation_player.h"
#include "LcdControl.h"
#include <thread>
#include <chrono>
#include <iostream>

AnimationPlayer::AnimationPlayer(std::vector<AnimationFrame>&& frames)
    : frames_(std::move(frames)), running_(true) {}

AnimationPlayer* AnimationPlayer::start(std::vector<AnimationFrame>&& frames) {
    AnimationPlayer* player = new AnimationPlayer(std::move(frames));
    player->thread_ = std::thread(&AnimationPlayer::run_loop, player);
    return player;
}

void AnimationPlayer::run_loop() {
    while (running_) {
        for (const auto& frame : frames_) {
            if (!running_) break;
            LcdData lcd_data;
            lcd_data.buffer = frame.buffer;
            lcd_data.side = LcdLeft;
            LcdControl::writeLcd(&lcd_data);
            lcd_data.side = LcdRight;
            LcdControl::writeLcd(&lcd_data);
            std::this_thread::sleep_for(std::chrono::milliseconds(frame.delay_ms));
        }
        if (running_) std::this_thread::sleep_for(std::chrono::milliseconds(3000));
    }
    // cleanup owned buffers
    for (auto& f : frames_) {
        delete[] f.buffer;
        f.buffer = nullptr;
    }
}

void AnimationPlayer::stop() {
    if (!running_) return;
    running_ = false;
    if (thread_.joinable()) thread_.join();
    delete this;
}

AnimationPlayer::~AnimationPlayer() {
    // if still running, ensure stop
    running_ = false;
    if (thread_.joinable()) thread_.join();
}
