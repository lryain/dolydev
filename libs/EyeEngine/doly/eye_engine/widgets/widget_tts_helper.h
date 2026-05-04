#pragma once

#include <functional>
#include <mutex>
#include <string>

namespace doly {
namespace eye_engine {

struct WidgetEdgeTtsConfig {
    bool enabled = false;
    std::string transport = "zmq";              // zmq | cmd
    std::string endpoint = "ipc:///tmp/doly_tts_req.sock";
    std::string client_bin = "python3 /home/pi/dev/dolydev/libs/tts/tts-server/edge_tts_client.py";
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
    int retries = 1;
};

struct WidgetTtsConfig {
    std::string engine = "edge-tts";  // edge-tts | espeak-ng
    std::string espeak_command = "espeak-ng -v zh '{text}' >/dev/null 2>&1";
    WidgetEdgeTtsConfig edge_tts{};
};

class WidgetTtsHelper {
public:
    using TtsListener = std::function<void(const std::string& log_tag,
                                           const WidgetTtsConfig& config,
                                           bool is_final)>;

    static void setListener(TtsListener listener);

    static void speak(const std::string& text,
                      bool is_final,
                      const WidgetTtsConfig& config,
                      const std::string& log_tag);

private:
    static std::string escapeForShell(const std::string& input);
    static std::string applyTemplate(const std::string& templ, const std::string& text);
    static void speakWithEspeak(const std::string& command_template,
                                const std::string& text,
                                const std::string& log_tag);
    static void speakWithEdgeTtsCmd(const WidgetEdgeTtsConfig& config,
                                    const std::string& text,
                                    const std::string& log_tag);
    static void speakWithEdgeTtsZmq(const WidgetEdgeTtsConfig& config,
                                    const std::string& text,
                                    const std::string& log_tag);

    static std::mutex listener_mutex_;
    static TtsListener listener_;
};

}  // namespace eye_engine
}  // namespace doly
