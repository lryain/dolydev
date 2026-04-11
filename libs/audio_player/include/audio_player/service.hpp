#pragma once

#include "audio_player/config.hpp"

#include <miniaudio.h>
#include <zmq.hpp>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace doly {
namespace audio {

class AudioPlayerService {
public:
    explicit AudioPlayerService(AudioPlayerConfig config);
    ~AudioPlayerService();

    bool initialize();
    void run();
    void stop();

private:
    struct StreamContext;
    struct StreamRequest;
    enum class SoundKind { File, Stream };

    // 重复播放模式：single|repeat_count|repeat_duration
    struct PlaybackControl {
        std::string playback_mode = "single";      // single|repeat_count|repeat_duration
        int play_count = 1;                        // repeat_count 模式下的总次数
        uint64_t play_duration_ms = 0;             // repeat_duration 模式下的总时长（ms）
        uint32_t repeat_interval_ms = 0;           // 重复间隔（ms）
        uint32_t repeat_fade_out_ms = 0;           // 淡出时长（ms）
        
        // 运行时追踪
        int current_play_count = 0;                // 已播放次数
        uint64_t playback_start_ts = 0;            // 整个播放周期的开始时间
        bool is_in_repeat_interval = false;        // 是否处于重复间隔中
        uint64_t interval_start_ts = 0;            // 间隔开始时间
    };

    struct ActiveSound {
        std::string id;            // 🆕 唯一标识符 (UUID/Serial)
        std::string alias;
        std::string uri;           // 🆕 记录对应的 URI
        int priority;
        uint64_t start_ts;
        std::unique_ptr<ma_sound, std::function<void(ma_sound*)>> handle;
        float base_volume{1.0f};
        float current_volume{1.0f};
        SoundKind kind{SoundKind::File};
        std::shared_ptr<StreamContext> stream;
        std::string stream_id;
        bool causes_ducking{false};
        bool preemptive{false};
        PlaybackControl playback_control;
    };

    bool handle_command(const std::string& payload, std::string& reply);
    void publish_status();
    std::string resolve_path(const std::string& alias_or_uri) const;
    int resolve_priority(const std::string& alias, int requested_priority) const;

    bool ensure_concurrency_budget(int requested_priority);
    void apply_preemption(int priority, bool preemptive);
    void apply_ducking_state();
    bool start_file_sound(const std::string& path,
                          const std::string& alias,
                          int priority,
                          float volume,
                          bool ducking,
                          bool preemptive);
    // 🆕 扩展版本，支持重复播放
    std::string start_file_sound(const std::string& path,
                                const std::string& alias,
                                int priority,
                                float volume,
                                bool ducking,
                                bool preemptive,
                                const std::string& playback_mode,
                                int play_count,
                                uint64_t play_duration_ms,
                                uint32_t repeat_interval_ms,
                                uint32_t repeat_fade_out_ms);
    bool begin_stream(const StreamRequest& request, std::string& stream_id, std::string& error);
    bool end_stream(const std::string& stream_id);
    void handle_stream_packets();
    bool prune_finished_sounds();
    void stop_alias(const std::string& alias);
    void stop_by_id(const std::string& id);    // 🆕 按 ID 停止
    void stop_by_uri(const std::string& uri);  // 🆕 按 URI 停止
    void stop_all_sounds();
    void schedule_clear_playing();
    void cancel_clear_playing();
    bool configure_playback_device();
    void cleanup();
    
    // 🆕 重复播放更新（在主循环中调用）
    void update_repeat_sounds();
    
    // 🆕 辅助：生成 ID
    std::string generate_sound_id();
    uint64_t next_sound_id_{1};

    AudioPlayerConfig config_;
    ma_engine engine_{};
    ma_context audio_context_{};
    zmq::context_t context_;
    zmq::socket_t status_pub_;
    zmq::socket_t cmd_rep_;
    zmq::socket_t stream_pull_;
    std::atomic<bool> running_{false};
    bool engine_ready_ = false;
    bool sockets_ready_ = false;
    bool stream_socket_ready_ = false;
    bool cleanup_done_ = false;
    struct QueuedSound {
        std::string path;
        std::string alias;
        int priority;
        float volume;
        bool ducking;
        bool preemptive;
        std::string playback_mode;
        int play_count;
        uint64_t play_duration_ms;
        uint32_t repeat_interval_ms;
        uint32_t repeat_fade_out_ms;
    };

    std::vector<ActiveSound> active_sounds_;
    std::vector<QueuedSound> sound_queue_;     // 🆕 音频队列
    ma_device_id playback_device_id_{};
    bool has_custom_device_id_ = false;
    bool audio_context_ready_ = false;
    uint64_t stream_id_counter_ = 0;
    std::atomic<uint64_t> playing_clear_at_ms_{0};
};

} // namespace audio
} // namespace doly
