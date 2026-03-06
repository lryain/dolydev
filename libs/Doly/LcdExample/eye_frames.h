#pragma once
#include <vector>
#include <string>
#include <cstdint>
#include "animation_player.h"

// Lightweight frame loader API used by pylcd. Implementations should return
// owned buffers (allocated with new uint8_t[] of size LcdControl::getBufferSize()).
std::vector<std::string> getPngFiles(const std::string& folder_path);

std::vector<AnimationFrame> LoadEyeAnimationFrames();

std::vector<AnimationFrame> LoadEyeAnimationFrames_test(const char* input, int frame_count, int delay_ms);

std::vector<AnimationFrame> LoadEyeAnimationFrames(char* frame_files[], int frame_delays[], int frame_count);

const char** createFrameFilesArray(const std::vector<std::string>& files);
