#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace doly {
namespace audio {

struct AudioPlayerConfig {
    std::string device = "doly_playback";
    std::string cmd_endpoint = "ipc:///tmp/doly_audio_player_cmd.sock";
    std::string status_topic = "status.audio.playback";
    std::string status_endpoint = "ipc:///tmp/doly_audio_player_status.sock";
    std::string stream_endpoint = "ipc:///tmp/doly_audio_player_stream.sock";
    uint32_t max_concurrent_sounds = 3;
    uint32_t stream_buffer_ms = 2000;
    // When playback stops, hold `is_playing` for this many ms to avoid flapping
    uint32_t capture_hold_ms = 200;
    float ducking_level = 0.35f;
    std::vector<std::string> preload_aliases;
    std::unordered_map<std::string, std::string> alias_paths;
    std::unordered_map<std::string, int> priority_map;
    std::string path_prefix;
};

AudioPlayerConfig load_config(const std::string& path);

} // namespace audio
} // namespace doly
