#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace zmq {
class context_t;
class socket_t;
}

namespace doly {
namespace eye_engine {

class WidgetAudioPlayer {
public:
    struct Config {
        std::string endpoint = "ipc:///tmp/doly_audio_player_cmd.sock";
        int req_timeout_ms = 150;
    };

    explicit WidgetAudioPlayer(std::string tag);
    ~WidgetAudioPlayer();

    void setConfig(const Config& config);
    void reset();

    bool playClip(const std::string& clip,
                  int priority,
                  bool preempt,
                  const std::string& playback_mode = "single",
                  int play_count = 1,
                  std::uint32_t repeat_interval_ms = 0,
                  std::uint32_t repeat_fade_out_ms = 0,
                  std::uint64_t play_duration_ms = 0);

    bool stopClip(const std::string& clip);

private:
    bool ensureSocket();
    bool isUri(const std::string& clip) const;

    std::string tag_;
    Config config_{};
    std::unique_ptr<zmq::context_t> zmq_ctx_;
    std::unique_ptr<zmq::socket_t> zmq_req_;
};

}  // namespace eye_engine
}  // namespace doly
