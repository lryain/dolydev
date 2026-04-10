#pragma once

#include <cstdint>
#include <string>

namespace doly {
namespace eye_engine {

struct EdgeTtsRequest {
    std::string endpoint = "ipc:///tmp/doly_tts_req.sock";
    std::string text;
    std::string voice = "zh-CN-YunxiaNeural";
    std::string pitch = "+0Hz";
    std::string rate = "+0%";
    std::string volume = "+0%";
    std::string format = "wav";
    bool play = true;
    std::string play_mode = "audio_player";
    std::string audio_player_endpoint = "ipc:///tmp/doly_audio_player_cmd.sock";
    int priority = 50;
    float audio_player_volume = 1.0f;
    std::string alias = "tts";
    bool ducking = true;
    int timeout_ms = 10000;
};

struct EdgeTtsResult {
    bool ok = false;
    std::string path;
    std::string error;
    std::string raw_response;
};

class WidgetEdgeTtsClient {
public:
    static EdgeTtsResult synthesizeOnce(const EdgeTtsRequest& request);
};

}  // namespace eye_engine
}  // namespace doly
