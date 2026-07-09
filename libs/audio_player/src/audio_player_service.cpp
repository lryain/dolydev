#include "audio_player/service.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <sys/stat.h>
#include <errno.h>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>
#include <capture_control.h>

namespace doly {
namespace audio {

namespace {

uint64_t now_millis() {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
}

std::string strip_file_scheme(const std::string& path) {
    constexpr const char* prefix = "file://";
    if (path.rfind(prefix, 0) == 0) {
        return path.substr(std::strlen(prefix));
    }
    return path;
}

struct SoundDeleter {
    void operator()(ma_sound* sound) const {
        if (sound) {
            ma_sound_uninit(sound);
            delete sound;
        }
    }
};

struct StreamDataSource {
    ma_data_source_base base{};
    ma_pcm_rb ring{};
    ma_format format{ma_format_f32};
    ma_uint32 channels{1};
    ma_uint32 sample_rate{16000};
    std::mutex mutex;
    ma_uint64 cursor{0};
    bool ended{false};
    bool initialized{false};
    bool ring_ready{false};
};

ma_result stream_on_read(ma_data_source* pDataSource, void* pFramesOut, ma_uint64 frameCount, ma_uint64* pFramesRead) {
    auto* source = reinterpret_cast<StreamDataSource*>(pDataSource);
    if (!source || !source->initialized) {
        if (pFramesRead) {
            *pFramesRead = 0;
        }
        return MA_AT_END;
    }

    const ma_uint32 frame_size = ma_get_bytes_per_sample(source->format) * source->channels;
    ma_uint64 total_frames_read = 0;

    std::unique_lock<std::mutex> lock(source->mutex, std::defer_lock);
    lock.lock();

    while (total_frames_read < frameCount) {
        ma_uint32 available = ma_pcm_rb_available_read(&source->ring);
        if (available == 0) {
            break;
        }

        ma_uint32 chunk = static_cast<ma_uint32>(std::min<ma_uint64>(available, frameCount - total_frames_read));
        if (chunk == 0) {
            break;
        }

        void* chunk_ptr = nullptr;
        ma_result result = ma_pcm_rb_acquire_read(&source->ring, &chunk, &chunk_ptr);
        if (result != MA_SUCCESS || chunk == 0) {
            ma_pcm_rb_commit_read(&source->ring, 0);
            break;
        }

        if (pFramesOut) {
            std::memcpy(static_cast<uint8_t*>(pFramesOut) + total_frames_read * frame_size, chunk_ptr, chunk * frame_size);
        }
        ma_pcm_rb_commit_read(&source->ring, chunk);
        total_frames_read += chunk;
    }

    source->cursor += total_frames_read;

    if (pFramesOut && total_frames_read < frameCount) {
        std::memset(static_cast<uint8_t*>(pFramesOut) + total_frames_read * frame_size, 0,
                    static_cast<size_t>(frameCount - total_frames_read) * frame_size);
    }

    if (pFramesRead) {
        *pFramesRead = total_frames_read;
    }

    if (total_frames_read == 0) {
        return source->ended ? MA_AT_END : MA_SUCCESS;
    }

    if (total_frames_read < frameCount && source->ended && ma_pcm_rb_available_read(&source->ring) == 0) {
        return MA_AT_END;
    }

    return MA_SUCCESS;
}

ma_result stream_on_seek(ma_data_source*, ma_uint64) {
    return MA_NOT_IMPLEMENTED;
}

ma_result stream_on_get_data_format(ma_data_source* pDataSource, ma_format* pFormat, ma_uint32* pChannels, ma_uint32* pSampleRate, ma_channel*, size_t) {
    auto* source = reinterpret_cast<StreamDataSource*>(pDataSource);
    if (!source || !source->initialized) {
        return MA_INVALID_OPERATION;
    }
    if (pFormat) {
        *pFormat = source->format;
    }
    if (pChannels) {
        *pChannels = source->channels;
    }
    if (pSampleRate) {
        *pSampleRate = source->sample_rate;
    }
    return MA_SUCCESS;
}

ma_result stream_on_get_cursor(ma_data_source* pDataSource, ma_uint64* pCursor) {
    auto* source = reinterpret_cast<StreamDataSource*>(pDataSource);
    if (!source || !source->initialized || !pCursor) {
        return MA_INVALID_OPERATION;
    }
    *pCursor = source->cursor;
    return MA_SUCCESS;
}

ma_result stream_on_get_length(ma_data_source* pDataSource, ma_uint64* pLength) {
    auto* source = reinterpret_cast<StreamDataSource*>(pDataSource);
    if (!source || !source->initialized) {
        return MA_INVALID_OPERATION;
    }
    if (!source->ended || ma_pcm_rb_available_read(&source->ring) > 0) {
        return MA_NOT_IMPLEMENTED;
    }
    if (pLength) {
        *pLength = source->cursor;
    }
    return MA_SUCCESS;
}

ma_result stream_on_set_looping(ma_data_source*, ma_bool32) {
    return MA_NOT_IMPLEMENTED;
}

const ma_data_source_vtable STREAM_VTABLE{
    stream_on_read,
    stream_on_seek,
    stream_on_get_data_format,
    stream_on_get_cursor,
    stream_on_get_length,
    stream_on_set_looping,
    0
};

ma_uint32 required_stream_buffer_frames(ma_uint32 sample_rate, uint32_t buffer_ms) {
    double frames = (static_cast<double>(sample_rate) * static_cast<double>(buffer_ms)) / 1000.0;
    frames = std::max(frames, static_cast<double>(sample_rate) * 0.5);
    frames = std::max(frames, 4096.0);
    return static_cast<ma_uint32>(frames);
}

std::string generate_stream_id(const std::string& alias, uint64_t counter) {
    std::ostringstream oss;
    oss << alias << "-" << now_millis() << "-" << counter;
    return oss.str();
}

bool device_matches(const std::string& target, const ma_device_info* info) {
    if (target.empty() || info == nullptr) {
        return false;
    }

    if (info->name != nullptr) {
        const std::string name(info->name);
        if (name == target || name.find(target) != std::string::npos) {
            return true;
        }
    }

    if (std::strlen(info->id.alsa) > 0 && target == info->id.alsa) {
        return true;
    }

    return false;
}

} // namespace

struct AudioPlayerService::StreamContext {
    StreamDataSource source{};
    ma_uint32 frame_size{0};
    bool reserved{false};
    uint64_t reserved_ts{0};

    ~StreamContext() {
        if (source.initialized) {
            ma_data_source_uninit(&source.base);
            source.initialized = false;
        }
        if (source.ring_ready) {
            ma_pcm_rb_uninit(&source.ring);
            source.ring_ready = false;
        }
    }
};

struct AudioPlayerService::StreamRequest {
    std::string alias;
    int sample_rate{16000};
    int channels{1};
    int priority{1};
    float volume{1.0f};
    bool ducking{false};
    bool preempt{false};
    std::string stream_id;
};

AudioPlayerService::AudioPlayerService(AudioPlayerConfig config)
    : config_(std::move(config)),
      context_(1),
      status_pub_(context_, ZMQ_PUB),
      cmd_rep_(context_, ZMQ_REP),
      stream_pull_(context_, ZMQ_PULL),
      running_(false) {}

AudioPlayerService::~AudioPlayerService() {
    stop();
    cleanup();
}

bool AudioPlayerService::initialize() {
    cleanup_done_ = false;

    if (engine_ready_) {
        return true;
    }

    if (!configure_playback_device()) {
        std::cerr << "[AudioPlayer] failed to configure playback device, using default" << std::endl;
    }

    auto engine_cfg = ma_engine_config_init();
    if (audio_context_ready_) {
        engine_cfg.pContext = &audio_context_;
    }
    if (has_custom_device_id_) {
        engine_cfg.pPlaybackDeviceID = &playback_device_id_;
    }
    if (ma_engine_init(&engine_cfg, &engine_) != MA_SUCCESS) {
        std::cerr << "[AudioPlayer] ma_engine_init failed" << std::endl;
        return false;
    }
    engine_ready_ = true;

    try {
        status_pub_.bind(config_.status_endpoint);
        cmd_rep_.bind(config_.cmd_endpoint);
        if (!config_.stream_endpoint.empty()) {
            stream_pull_.bind(config_.stream_endpoint);
            stream_socket_ready_ = true;
        }
        sockets_ready_ = true;
    } catch (const zmq::error_t& e) {
        std::cerr << "[AudioPlayer] ZeroMQ bind failed: " << e.what() << std::endl;
        cleanup();
        return false;
    }

    int rcvtimeo = 200;
    cmd_rep_.setsockopt(ZMQ_RCVTIMEO, &rcvtimeo, sizeof(rcvtimeo));

    // Ensure capture control shared memory exists so we can increment/decrement
    // playback_count to mute audio capture during playback.
    (void)capture_control_open("/doly_capture_control", true);

    return true;
}

void AudioPlayerService::run() {
    running_.store(true);
    auto status_deadline = std::chrono::steady_clock::now();

    while (running_.load()) {
        void* cmd_handle = cmd_rep_.handle();
        void* stream_handle = stream_pull_.handle();
        zmq::pollitem_t items[2] = {
            {cmd_handle, 0, ZMQ_POLLIN, 0},
            {stream_handle, 0, ZMQ_POLLIN, 0}
        };
        const int poll_count = stream_socket_ready_ ? 2 : 1;
        zmq::poll(items, poll_count, 200);

        if (items[0].revents & ZMQ_POLLIN) {
            zmq::message_t request;
            if (cmd_rep_.recv(request, zmq::recv_flags::none)) {
                std::string payload(static_cast<char*>(request.data()), request.size());
                std::string reply;
                if (handle_command(payload, reply)) {
                    cmd_rep_.send(zmq::buffer(reply), zmq::send_flags::none);
                }
            }
        }

        if (stream_socket_ready_ && (items[1].revents & ZMQ_POLLIN)) {
            handle_stream_packets();
        }

        if (prune_finished_sounds()) {
            apply_ducking_state();
        }

        // 🆕 更新重复播放状态
        update_repeat_sounds();

        const auto now = std::chrono::steady_clock::now();
        if (now >= status_deadline) {
            publish_status();
            status_deadline = now + std::chrono::seconds(1);
        }
    }

    cleanup();
}

void AudioPlayerService::stop() {
    running_.store(false);
}

bool AudioPlayerService::handle_command(const std::string& payload, std::string& reply) {
    nlohmann::json response = {{"ok", true}};

    nlohmann::json request;
    try {
        request = nlohmann::json::parse(payload);
    } catch (const std::exception& e) {
        response = {{"ok", false}, {"error", std::string("invalid_json: ") + e.what()}};
        reply = response.dump();
        return true;
    }

    const std::string action = request.value("action", request.value("topic", ""));
    if (action == "cmd.audio.play" || action == "play") {
        const std::string alias = request.value("alias", "");
        const std::string uri = request.value("uri", request.value("path", ""));
        // If alias is provided and it's a known alias in config, resolve by alias;
        // otherwise prefer uri to resolve an explicit file:// scheme. This allows
        // clients to pass a unique alias for status matching while supplying a file URI.
        std::string candidate;
        if (!alias.empty() && config_.alias_paths.find(alias) != config_.alias_paths.end()) {
            candidate = alias;
        } else {
            candidate = uri.empty() ? alias : uri;
        }
        const std::string resolved = resolve_path(candidate);

        if (resolved.empty()) {
            response = {{"ok", false}, {"error", "missing_path_or_alias"}};
            reply = response.dump();
            return true;
        }

        const int priority = resolve_priority(alias, request.value("priority", 9));
        const float volume = request.value("volume", 1.0f);
        const bool ducking = request.value("ducking", false);
        const bool preempt = request.value("preempt", false);

        // 🆕 重复播放参数解析
        const std::string playback_mode = request.value("playback_mode", "single");
        const int play_count = request.value("play_count", 1);
        const uint64_t play_duration_ms = request.value("play_duration_ms", 0);
        const uint32_t repeat_interval_ms = request.value("repeat_interval_ms", 0);
        const uint32_t repeat_fade_out_ms = request.value("repeat_fade_out_ms", 0);

        // 🆕 统一入队处理，确保即使并发未满也要遵循优先级排队逻辑
        QueuedSound q;
        q.path = resolved;
        q.alias = alias;
        q.priority = priority;
        q.volume = volume;
        q.ducking = ducking;
        q.preemptive = preempt;
        q.playback_mode = playback_mode;
        q.play_count = play_count;
        q.play_duration_ms = play_duration_ms;
        q.repeat_interval_ms = repeat_interval_ms;
        q.repeat_fade_out_ms = repeat_fade_out_ms;
        sound_queue_.push_back(std::move(q));
        
        // 保持队列按优先级排序 (0 最高，越大越低)
        std::stable_sort(sound_queue_.begin(), sound_queue_.end(), 
            [](const QueuedSound& a, const QueuedSound& b) {
                return a.priority < b.priority;
            });

        // 尝试启动声音（prune_finished_sounds 会从队列中按优先级取出音频并启动）
        prune_finished_sounds();

        // 查找是否成功启动
        std::string sound_id = "";
        for (const auto& sound : active_sounds_) {
            if (sound.alias == alias && (now_millis() - sound.start_ts < 100)) {
                sound_id = sound.id;
                break;
            }
        }

        if (sound_id.empty()) {
            response["status"] = "queued";
            response["id"] = "queued-" + std::to_string(now_millis());
            std::cerr << "[AudioPlayer] sound queued and sorted: alias=" << alias << " priority=" << priority << std::endl;
        } else {
            response["id"] = sound_id;
            response["path"] = resolved;
            response["alias"] = alias;
            response["priority"] = priority;
            response["playback_mode"] = playback_mode;
            response["play_count"] = play_count;
            response["play_duration_ms"] = play_duration_ms;
            response["repeat_interval_ms"] = repeat_interval_ms;
        }
    } else if (action == "cmd.audio.stop" || action == "stop") {
        const std::string alias = request.value("alias", "");
        const std::string id = request.value("id", "");
        const std::string uri = request.value("uri", "");

        if (alias == "all" || uri == "all" || (alias.empty() && id.empty() && uri.empty())) {
            stop_all_sounds();
        } else if (!id.empty()) {
            stop_by_id(id);
        } else if (!uri.empty()) {
            stop_by_uri(uri);
        } else {
            stop_alias(alias);
        }
    } else if (action == "cmd.audio.status" || action == "status") {
        response["status"] = {
            {"active_sounds", static_cast<int>(active_sounds_.size())},
            {"timestamp", now_millis()}
        };
    } else if (action == "cmd.audio.stream_begin" || action == "stream_begin") {
        StreamRequest req;
        req.alias = request.value("alias", "stream");
        req.sample_rate = request.value("sample_rate", 16000);
        req.channels = request.value("channels", 1);
        req.priority = resolve_priority(req.alias, request.value("priority", 0));
        req.volume = request.value("volume", 1.0f);
        req.ducking = request.value("ducking", false);
        req.preempt = request.value("preempt", false);
        req.stream_id = request.value("stream_id", "");

        std::string stream_id;
        std::string error;
        if (!begin_stream(req, stream_id, error)) {
            response = {{"ok", false}, {"error", error}};
        } else {
            response["stream_id"] = stream_id;
            response["alias"] = req.alias;
            response["priority"] = req.priority;
        }
    } else if (action == "cmd.audio.stream_end" || action == "stream_end") {
        const std::string stream_id = request.value("stream_id", "");
        if (stream_id.empty()) {
            response = {{"ok", false}, {"error", "missing_stream_id"}};
        } else if (!end_stream(stream_id)) {
            response = {{"ok", false}, {"error", "unknown_stream"}};
        } else {
            response["stream_id"] = stream_id;
        }
    } else {
        response = {{"ok", false}, {"error", "unsupported_action"}};
    }

    reply = response.dump();
    return true;
}

void AudioPlayerService::publish_status() {
    if (!sockets_ready_ || !status_pub_) {
        return;
    }

    nlohmann::json payload = {
        {"ts", now_millis()},
        {"running", running_.load()},
        {"active_sounds", static_cast<int>(active_sounds_.size())}
    };

    nlohmann::json sounds = nlohmann::json::array();
    for (const auto& sound : active_sounds_) {
        nlohmann::json item = {
            {"id", sound.id},
            {"alias", sound.alias},
            {"uri", sound.uri},
            {"priority", sound.priority},
            {"started", sound.start_ts},
            {"volume", sound.current_volume},
            {"base_volume", sound.base_volume},
            {"ducking", sound.causes_ducking},
            {"preempt", sound.preemptive},
            {"kind", sound.kind == SoundKind::Stream ? "stream" : "file"}
        };
        if (sound.kind == SoundKind::Stream) {
            item["stream_id"] = sound.stream_id;
            if (sound.stream && sound.stream->source.initialized) {
                std::unique_lock<std::mutex> lock(sound.stream->source.mutex);
                const ma_uint32 readable = ma_pcm_rb_available_read(&sound.stream->source.ring);
                const ma_uint32 writable = ma_pcm_rb_available_write(&sound.stream->source.ring);
                item["buffer_frames"] = readable;
                item["buffer_capacity"] = readable + writable;
            }
        }
        sounds.push_back(std::move(item));
    }

    payload["sounds"] = sounds;

    std::string serialized = payload.dump();
    zmq::message_t topic_msg(config_.status_topic.data(), config_.status_topic.size());
    zmq::message_t data_msg(serialized.data(), serialized.size());

    try {
        status_pub_.send(topic_msg, zmq::send_flags::sndmore);
        status_pub_.send(data_msg, zmq::send_flags::none);
    } catch (const zmq::error_t& e) {
        std::cerr << "[AudioPlayer] publish_status failed: " << e.what() << std::endl;
    }
}

std::string AudioPlayerService::resolve_path(const std::string& alias_or_uri) const {
    if (alias_or_uri.empty()) {
        return {};
    }

    const auto it = config_.alias_paths.find(alias_or_uri);
    if (it != config_.alias_paths.end()) {
        std::string alias_target = it->second;
        if (!alias_target.empty() && alias_target.front() != '/' && !config_.path_prefix.empty()) {
            return config_.path_prefix + alias_target;
        }
        return alias_target;
    }

    return strip_file_scheme(alias_or_uri);
}

int AudioPlayerService::resolve_priority(const std::string& alias, int requested_priority) const {
    if (!alias.empty()) {
        auto it = config_.priority_map.find(alias);
        if (it != config_.priority_map.end()) {
            return it->second;
        }
    }
    // 0 is highest, 9 is default
    return requested_priority >= 0 ? requested_priority : 9;
}

void AudioPlayerService::apply_preemption(int priority, bool preemptive) {
    if (!preemptive) {
        return;
    }

    bool changed = false;
    for (auto it = active_sounds_.begin(); it != active_sounds_.end();) {
        // Now lower value means higher priority. Preempt if active is lower priority (higher value).
        if (it->priority > priority) {
            if (it->handle) {
                ma_sound_stop(it->handle.get());
            }
            it = active_sounds_.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }

    if (changed) {
        apply_ducking_state();
    }
}

void AudioPlayerService::apply_ducking_state() {
    const bool has_ducker = std::any_of(active_sounds_.begin(), active_sounds_.end(),
        [](const ActiveSound& sound) {
            return sound.causes_ducking;
        });

    for (auto& sound : active_sounds_) {
        if (!sound.handle) {
            continue;
        }

        float target = sound.base_volume;
        if (has_ducker && !sound.causes_ducking) {
            target = sound.base_volume * config_.ducking_level;
        }

        if (std::fabs(sound.current_volume - target) > 1e-4f) {
            ma_sound_set_volume(sound.handle.get(), target);
            sound.current_volume = target;
        }
    }
}

bool AudioPlayerService::start_file_sound(const std::string& path,
                                          const std::string& alias,
                                          int priority,
                                          float volume,
                                          bool ducking,
                                          bool preempt) {
    apply_preemption(priority, preempt);

    if (!ensure_concurrency_budget(priority)) {
        return false;
    }

    // 检查文件是否存在并可读，若不存在则返回明确错误并记录
    struct stat st;
    if (stat(path.c_str(), &st) != 0) {
        std::cerr << "[AudioPlayer] start_file_sound: file not accessible: " << path
                  << " errno=" << errno << " (" << strerror(errno) << ")" << std::endl;
        return false;
    }

    auto handle = std::unique_ptr<ma_sound, SoundDeleter>(new ma_sound{});
    ma_result r = ma_sound_init_from_file(&engine_, path.c_str(), MA_SOUND_FLAG_STREAM, nullptr, nullptr, handle.get());
    if (r != MA_SUCCESS) {
        std::cerr << "[AudioPlayer] start_file_sound: ma_sound_init_from_file failed for '" << path << "' ma_result=" << r << std::endl;
        return false;
    }

    ma_sound_set_volume(handle.get(), volume);
    r = ma_sound_start(handle.get());
    if (r != MA_SUCCESS) {
        std::cerr << "[AudioPlayer] start_file_sound: ma_sound_start failed for '" << path << "' ma_result=" << r << std::endl;
        return false;
    }

    ActiveSound record;
    record.id = generate_sound_id();
    record.alias = alias;
    record.uri = path;
    record.priority = priority;
    record.start_ts = now_millis();
    record.handle = std::move(handle);
    record.base_volume = volume;
    record.current_volume = volume;
    record.kind = SoundKind::File;
    record.causes_ducking = ducking;
    record.preemptive = preempt;

    active_sounds_.push_back(std::move(record));
    // Indicate to other processes that playback is active (mute capture)
    capture_control_inc();
    capture_control_set_playing(true);
    // Cancel any scheduled clear
    playing_clear_at_ms_.store(0, std::memory_order_relaxed);
    apply_ducking_state();
    // 立即发布状态，保证客户端能马上看到新加入的声音（避免 race 条件）
    publish_status();

    return true;
}

// 🆕 支持重复播放的 start_file_sound 扩展版本
std::string AudioPlayerService::start_file_sound(const std::string& path,
                                                const std::string& alias,
                                                int priority,
                                                float volume,
                                                bool ducking,
                                                bool preemptive,
                                                const std::string& playback_mode,
                                                int play_count,
                                                uint64_t play_duration_ms,
                                                uint32_t repeat_interval_ms,
                                                uint32_t repeat_fade_out_ms) {
    // 参数验证
    if (playback_mode == "repeat_count" && play_count <= 0) {
        std::cerr << "[AudioPlayer] repeat_count mode requires play_count > 0" << std::endl;
        return "";
    }
    if (playback_mode == "repeat_duration" && play_duration_ms <= 0) {
        std::cerr << "[AudioPlayer] repeat_duration mode requires play_duration_ms > 0" << std::endl;
        return "";
    }

    // 调用原有逻辑
    if (!start_file_sound(path, alias, priority, volume, ducking, preemptive)) {
        return "";
    }

    // 🆕 配置重复播放参数（最后一个音频元素）
    if (!active_sounds_.empty()) {
        auto& last_sound = active_sounds_.back();
        last_sound.playback_control.playback_mode = playback_mode;
        last_sound.playback_control.play_count = play_count;
        last_sound.playback_control.play_duration_ms = play_duration_ms;
        last_sound.playback_control.repeat_interval_ms = repeat_interval_ms;
        last_sound.playback_control.repeat_fade_out_ms = repeat_fade_out_ms;
        last_sound.playback_control.current_play_count = 1;  // 首次播放
        last_sound.playback_control.playback_start_ts = now_millis();

        std::cerr << "[AudioPlayer] start_file_sound with repeat: alias=" << alias
                  << " id=" << last_sound.id
                  << " mode=" << playback_mode
                  << " play_count=" << play_count
                  << " play_duration_ms=" << play_duration_ms
                  << " repeat_interval_ms=" << repeat_interval_ms << std::endl;
        
        return last_sound.id;
    }

    return "";
}

bool AudioPlayerService::begin_stream(const StreamRequest& request, std::string& stream_id, std::string& error) {
    if (prune_finished_sounds()) {
        apply_ducking_state();
    }

    apply_preemption(request.priority, request.preempt);


    // Prepare a stream context but if concurrency budget is not available
    // create a short-lived reservation slot. The actual ring/data-source
    // and ma_sound will be initialized on the first incoming packet.

    auto stream_ctx = std::make_shared<StreamContext>();
    stream_ctx->source.format = ma_format_f32;
    stream_ctx->source.channels = static_cast<ma_uint32>(request.channels);
    stream_ctx->source.sample_rate = static_cast<ma_uint32>(request.sample_rate);
    stream_ctx->frame_size = ma_get_bytes_per_sample(stream_ctx->source.format) * stream_ctx->source.channels;

    const ma_uint32 buffer_frames = required_stream_buffer_frames(stream_ctx->source.sample_rate, config_.stream_buffer_ms);

    if (!ensure_concurrency_budget(request.priority)) {
        // create a reservation: don't initialize ring/ma_data_source/ma_sound yet,
        // but add an ActiveSound placeholder so client can start pushing data.
        stream_ctx->reserved = true;
        stream_ctx->reserved_ts = now_millis();

        std::string generated_id = request.stream_id.empty() ? generate_stream_id(request.alias, stream_id_counter_++) : request.stream_id;
        stream_id = generated_id;

        ActiveSound record;
        record.id = generate_sound_id();
        record.alias = request.alias;
        record.priority = request.priority;
        record.start_ts = now_millis();
        record.handle = nullptr;
        record.base_volume = request.volume;
        record.current_volume = request.volume;
        record.kind = SoundKind::Stream;
        record.stream = stream_ctx;
        record.stream_id = generated_id;
        record.causes_ducking = request.ducking;
        record.preemptive = request.preempt;

        active_sounds_.push_back(std::move(record));
        apply_ducking_state();
        publish_status();
    capture_control_inc();
    // As we started a reservation, cancel scheduled clear
    playing_clear_at_ms_.store(0, std::memory_order_relaxed);
    std::cerr << "[AudioPlayer] reservation created for stream_id: " << generated_id
          << " id=" << active_sounds_.back().id
          << " priority=" << request.priority << " reserved_ts=" << stream_ctx->reserved_ts << std::endl;

        return true;
    }

    // There is capacity: fully initialize ring, data source and start sound now.
    if (ma_pcm_rb_init(stream_ctx->source.format,
                       stream_ctx->source.channels,
                       buffer_frames,
                       nullptr,
                       nullptr,
                       &stream_ctx->source.ring) != MA_SUCCESS) {
        error = "ring_buffer_init_failed";
        return false;
    }
    stream_ctx->source.ring_ready = true;

    ma_data_source_config ds_config = ma_data_source_config_init();
    ds_config.vtable = &STREAM_VTABLE;

    if (ma_data_source_init(&ds_config, &stream_ctx->source.base) != MA_SUCCESS) {
        error = "data_source_init_failed";
        return false;
    }

    stream_ctx->source.initialized = true;
    stream_ctx->source.ended = false;

    auto handle = std::unique_ptr<ma_sound, SoundDeleter>(new ma_sound{});
    if (ma_sound_init_from_data_source(&engine_, &stream_ctx->source.base, MA_SOUND_FLAG_STREAM, nullptr, handle.get()) != MA_SUCCESS) {
        error = "sound_init_failed";
        return false;
    }

    ma_sound_set_volume(handle.get(), request.volume);
    if (ma_sound_start(handle.get()) != MA_SUCCESS) {
        error = "sound_start_failed";
        return false;
    }

    std::string generated_id = request.stream_id.empty() ? generate_stream_id(request.alias, stream_id_counter_++) : request.stream_id;
    stream_id = generated_id;

    ActiveSound record;
    record.id = generate_sound_id();
    record.alias = request.alias;
    record.priority = request.priority;
    record.start_ts = now_millis();
    record.handle = std::move(handle);
    record.base_volume = request.volume;
    record.current_volume = request.volume;
    record.kind = SoundKind::Stream;
    record.stream = stream_ctx;
    record.stream_id = generated_id;
    record.causes_ducking = request.ducking;
    record.preemptive = request.preempt;

    active_sounds_.push_back(std::move(record));
    apply_ducking_state();
    // 立即发布状态，保证客户端能马上看到新加入的流（避免 race 条件）
    publish_status();
    capture_control_inc();
    capture_control_set_playing(true);
    // Cancel any scheduled clear
    playing_clear_at_ms_.store(0, std::memory_order_relaxed);
    // (debug logging removed)

    return true;
}

bool AudioPlayerService::end_stream(const std::string& stream_id) {
    bool removed = false;
    for (auto it = active_sounds_.begin(); it != active_sounds_.end();) {
        if (it->stream_id == stream_id) {
            if (it->handle) {
                ma_sound_stop(it->handle.get());
            }
            it = active_sounds_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }

        if (removed) {
        apply_ducking_state();
        // schedule a delayed clear as in prune_finished_sounds
        if (active_sounds_.empty()) {
            if (config_.capture_hold_ms == 0) {
                capture_control_set_playing(false);
            } else {
                uint64_t at = playing_clear_at_ms_.load(std::memory_order_relaxed);
                uint64_t now = now_millis();
                if (at == 0) {
                    playing_clear_at_ms_.store(now + config_.capture_hold_ms, std::memory_order_relaxed);
                }
            }
        } else {
            playing_clear_at_ms_.store(0, std::memory_order_relaxed);
        }
    }

    return removed;
}

bool AudioPlayerService::ensure_concurrency_budget(int requested_priority) {
    if (active_sounds_.empty()) {
        return true;
    }

    int highest_active_priority = 999;
    int lowest_active_priority = -1;
    for (const auto& s : active_sounds_) {
        // lower value is higher priority
        highest_active_priority = std::min(highest_active_priority, s.priority);
        lowest_active_priority = std::max(lowest_active_priority, s.priority);
    }

    // If requested sound has lower priority (larger number) than any active sound, 
    // it must wait if the highest priority sound is high enough (e.g. 0-2 range or just standard rule).
    // Current Rule: cannot interrupt a higher priority sound (smaller number).
    if (requested_priority > highest_active_priority) {
        return false;
    }

    if (active_sounds_.size() < config_.max_concurrent_sounds) {
        return true;
    }

    // Capacity full: kick out the lowest priority (largest number) if requested is higher (smaller number).
    if (requested_priority < lowest_active_priority) {
        auto victim = std::max_element(active_sounds_.begin(), active_sounds_.end(),
            [](const ActiveSound& a, const ActiveSound& b) {
                if (a.priority != b.priority) {
                    // want largest priority number
                    return a.priority < b.priority;
                }
                // then oldest
                return a.start_ts > b.start_ts;
            });
        if (victim != active_sounds_.end()) {
            if (victim->handle) {
                ma_sound_stop(victim->handle.get());
            }
            capture_control_dec();
            active_sounds_.erase(victim);
            apply_ducking_state();
            return true;
        }
    }

    return false;
}

bool AudioPlayerService::prune_finished_sounds() {
    bool removed = false;
    const uint64_t now = now_millis();
    for (auto it = active_sounds_.begin(); it != active_sounds_.end();) {
        // 如果声音刚刚创建（小于保护期），不要立刻删除，避免 race 条件
        const uint64_t age_ms = now - it->start_ts;
        const uint64_t protection_ms = 200; // 保护期 200ms
        bool finished = false;
        if (it->kind == SoundKind::Stream) {
            // 对于流类型，只有当 stream source 标记为 ended 且播放停止后才认为 finished
            bool playing_stopped = !it->handle || ma_sound_is_playing(it->handle.get()) == MA_FALSE;
            bool stream_ended = true;
            if (it->stream) {
                std::unique_lock<std::mutex> lock(it->stream->source.mutex);
                stream_ended = it->stream->source.ended;
            }
            finished = playing_stopped && stream_ended && (age_ms > protection_ms);
            // 如果这是一个保留位且长时间（例如 5s）没有填充数据，则回收
            if (it->stream && it->stream->reserved) {
                const uint64_t reserved_age = now_millis() - it->stream->reserved_ts;
                const uint64_t reservation_timeout_ms = 5000;
                if (reserved_age > reservation_timeout_ms) {
                    finished = true;
                }
            }
        } else {
            const bool playing_stopped = (!it->handle || ma_sound_is_playing(it->handle.get()) == MA_FALSE);
            // 🆕 重复播放模式特殊处理：不立刻删除，交给 update_repeat_sounds 重启
            const auto& ctrl = it->playback_control;
            const bool repeat_count_mode = (ctrl.playback_mode == "repeat_count");
            const bool repeat_duration_mode = (ctrl.playback_mode == "repeat_duration");

            if (playing_stopped && (repeat_count_mode || repeat_duration_mode) && age_ms > protection_ms) {
                // repeat_count: 如果还没达到目标次数，进入间隔等待，不删除
                if (repeat_count_mode && ctrl.current_play_count < ctrl.play_count) {
                    if (!it->playback_control.is_in_repeat_interval) {
                        it->playback_control.is_in_repeat_interval = true;
                        it->playback_control.interval_start_ts = now;
                        std::cerr << "[AudioPlayer] prune: enter repeat interval alias=" << it->alias
                                  << " count=" << ctrl.current_play_count << "/" << ctrl.play_count << std::endl;
                    }
                    ++it;
                    continue;
                }

                // repeat_duration: 如果还在限定时长内，保持存活，由 update_repeat_sounds 重启
                if (repeat_duration_mode) {
                    const uint64_t elapsed = now - ctrl.playback_start_ts;
                    if (elapsed < ctrl.play_duration_ms) {
                        ++it;
                        continue;
                    }
                }
            }

            finished = playing_stopped && (age_ms > protection_ms);
        }
        if (finished) {
            // Playback stopped: decrement the capture control playback count
            capture_control_dec();
            it = active_sounds_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }

    // 🆕 调度逻辑：尝试从队列中启动音频
    // 注意：我们将启动逻辑限制在“非正在运行的启动动作中”，以防递归 Segfault
    static bool is_dispatching = false;
    if (is_dispatching) return removed;
    is_dispatching = true;

    while (!sound_queue_.empty() && active_sounds_.size() < config_.max_concurrent_sounds) {
        // 首先检查当前是否满足启动最高优先级音频的条件
        if (!ensure_concurrency_budget(sound_queue_.front().priority)) {
            break;
        }

        QueuedSound q = std::move(sound_queue_.front());
        sound_queue_.erase(sound_queue_.begin());

        std::cerr << "[AudioPlayer] dispatching queued sound: " << q.alias << " (priority=" << q.priority << ")" << std::endl;
        
        std::string started_id = start_file_sound(q.path, q.alias, q.priority, q.volume, q.ducking, q.preemptive,
                                                   q.playback_mode, q.play_count, q.play_duration_ms,
                                                   q.repeat_interval_ms, q.repeat_fade_out_ms);

        if (started_id.empty()) {
            continue;
        }
        
        removed = true;
        playing_clear_at_ms_.store(0, std::memory_order_relaxed);
    }

    is_dispatching = false;

    // If no active sounds remain, clear the is_playing flag to re-enable capture
    if (active_sounds_.empty()) {
        // schedule a delayed clear of the playing flag to avoid toggles
        if (config_.capture_hold_ms == 0) {
            capture_control_set_playing(false);
        } else {
            uint64_t at = playing_clear_at_ms_.load(std::memory_order_relaxed);
            uint64_t now = now_millis();
            if (at == 0) {
                playing_clear_at_ms_.store(now + config_.capture_hold_ms, std::memory_order_relaxed);
            } else if (now >= at) {
                capture_control_set_playing(false);
                playing_clear_at_ms_.store(0, std::memory_order_relaxed);
            }
        }
    } else {
        // There are active sounds, cancel any scheduled clear
        playing_clear_at_ms_.store(0, std::memory_order_relaxed);
    }
    return removed;
}

void AudioPlayerService::stop_alias(const std::string& alias) {
    if (alias.empty()) {
        return;
    }

    bool removed = false;
    for (auto it = active_sounds_.begin(); it != active_sounds_.end();) {
        if (it->alias == alias) {
            if (it->handle) {
                ma_sound_stop(it->handle.get());
            }
            // Decrement capture control since this sound is being removed
            capture_control_dec();
            it = active_sounds_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }

    if (removed) {
        apply_ducking_state();
        if (active_sounds_.empty()) {
            if (config_.capture_hold_ms == 0) {
                capture_control_set_playing(false);
            } else {
                uint64_t at = playing_clear_at_ms_.load(std::memory_order_relaxed);
                uint64_t now = now_millis();
                if (at == 0) {
                    playing_clear_at_ms_.store(now + config_.capture_hold_ms, std::memory_order_relaxed);
                }
            }
        } else {
            playing_clear_at_ms_.store(0, std::memory_order_relaxed);
        }
    }
}

void AudioPlayerService::stop_all_sounds() {
    for (auto& sound : active_sounds_) {
        if (sound.handle) {
            ma_sound_stop(sound.handle.get());
        }
        // Decrement capture control for each sound that was running
        capture_control_dec();
    }
    active_sounds_.clear();
    apply_ducking_state();
    if (config_.capture_hold_ms == 0) {
        capture_control_set_playing(false);
    } else {
        playing_clear_at_ms_.store(now_millis() + config_.capture_hold_ms, std::memory_order_relaxed);
    }
}

void AudioPlayerService::handle_stream_packets() {
    while (true) {
        zmq::message_t id_msg;
        if (!stream_pull_.recv(id_msg, zmq::recv_flags::dontwait)) {
            break;
        }

        std::string stream_id(static_cast<char*>(id_msg.data()), id_msg.size());

        int more = 0;
        size_t more_size = sizeof(more);
        stream_pull_.getsockopt(ZMQ_RCVMORE, &more, &more_size);
        if (!more) {
            std::cerr << "[AudioPlayer] stream chunk missing payload for stream_id: " << stream_id << std::endl;
            continue;
        }

        zmq::message_t data_msg;
        if (!stream_pull_.recv(data_msg, zmq::recv_flags::none)) {
            break;
        }

        auto it = std::find_if(active_sounds_.begin(), active_sounds_.end(),
            [&stream_id](const ActiveSound& sound) {
                return sound.stream_id == stream_id;
            });

        if (it == active_sounds_.end()) {
            std::cerr << "[AudioPlayer] stream packet for unknown stream_id: " << stream_id << std::endl;
            continue;
        }

        ActiveSound& sound = *it;
        if (!sound.stream) {
            std::cerr << "[AudioPlayer] invalid stream context for stream_id: " << stream_id << std::endl;
            continue;
        }

        auto& stream_ctx = sound.stream;

        // If this stream was only reserved (no ring/ma_sound initialized),
        // initialize now on first incoming non-empty packet.
        if (stream_ctx->reserved) {
            if (data_msg.size() == 0) {
                // empty end packet before any data -> mark ended and skip
                std::unique_lock<std::mutex> lock(stream_ctx->source.mutex);
                stream_ctx->source.ended = true;
                continue;
            }

            // initialize ring and data source and ma_sound now
            const ma_uint32 buffer_frames_local = required_stream_buffer_frames(stream_ctx->source.sample_rate, config_.stream_buffer_ms);
            if (ma_pcm_rb_init(stream_ctx->source.format,
                               stream_ctx->source.channels,
                               buffer_frames_local,
                               nullptr,
                               nullptr,
                               &stream_ctx->source.ring) != MA_SUCCESS) {
                std::cerr << "[AudioPlayer] ring_buffer_init_failed during reservation init for stream_id: " << stream_id << std::endl;
                // remove reservation
                std::cerr << "[AudioPlayer] removing reservation (ring init failed) for stream_id: " << stream_id << std::endl;
                end_stream(stream_id);
                continue;
            }
            stream_ctx->source.ring_ready = true;

            ma_data_source_config ds_config = ma_data_source_config_init();
            ds_config.vtable = &STREAM_VTABLE;
            if (ma_data_source_init(&ds_config, &stream_ctx->source.base) != MA_SUCCESS) {
                std::cerr << "[AudioPlayer] data_source_init_failed during reservation init for stream_id: " << stream_id << std::endl;
                end_stream(stream_id);
                continue;
            }
            stream_ctx->source.initialized = true;
            stream_ctx->source.ended = false;

            auto handle = std::unique_ptr<ma_sound, SoundDeleter>(new ma_sound{});
            if (ma_sound_init_from_data_source(&engine_, &stream_ctx->source.base, MA_SOUND_FLAG_STREAM, nullptr, handle.get()) != MA_SUCCESS) {
                std::cerr << "[AudioPlayer] sound_init_failed during reservation init for stream_id: " << stream_id << std::endl;
                std::cerr << "[AudioPlayer] removing reservation (sound init failed) for stream_id: " << stream_id << std::endl;
                end_stream(stream_id);
                continue;
            }
            ma_sound_set_volume(handle.get(), sound.base_volume);
            if (ma_sound_start(handle.get()) != MA_SUCCESS) {
                std::cerr << "[AudioPlayer] sound_start_failed during reservation init for stream_id: " << stream_id << std::endl;
                std::cerr << "[AudioPlayer] removing reservation (sound start failed) for stream_id: " << stream_id << std::endl;
                end_stream(stream_id);
                continue;
            }

            sound.handle = std::move(handle);
            stream_ctx->reserved = false;
            std::cerr << "[AudioPlayer] reservation initialized for stream_id: " << stream_id << " now active" << std::endl;
            // publish status now that the stream is fully active
            publish_status();
        }

        if (!stream_ctx->source.initialized || !stream_ctx->source.ring_ready) {
            std::cerr << "[AudioPlayer] invalid stream context for stream_id: " << stream_id << std::endl;
            continue;
        }

        auto& source = stream_ctx->source;

        if (data_msg.size() == 0) {
            std::unique_lock<std::mutex> lock(source.mutex);
            source.ended = true;
            continue;
        }

        if (sound.stream->frame_size == 0 || data_msg.size() % sound.stream->frame_size != 0) {
            std::cerr << "[AudioPlayer] misaligned stream payload for stream_id: " << stream_id << std::endl;
            continue;
        }

        const ma_uint32 frames = static_cast<ma_uint32>(data_msg.size() / sound.stream->frame_size);
        const uint8_t* pcm = static_cast<const uint8_t*>(data_msg.data());

        std::unique_lock<std::mutex> lock(source.mutex);

        ma_uint32 writable = ma_pcm_rb_available_write(&source.ring);
        if (writable < frames) {
            const ma_uint32 drop = frames - writable;
            if (drop > 0) {
                const ma_uint32 readable = ma_pcm_rb_available_read(&source.ring);
                ma_pcm_rb_seek_read(&source.ring, std::min(drop, readable));
            }
        }

        ma_uint32 frames_remaining = frames;
        const uint8_t* src_ptr = pcm;
        while (frames_remaining > 0) {
            ma_uint32 chunk = frames_remaining;
            void* dst = nullptr;
            ma_result acquire = ma_pcm_rb_acquire_write(&source.ring, &chunk, &dst);
            if (acquire != MA_SUCCESS || chunk == 0) {
                break;
            }

            std::memcpy(dst, src_ptr, chunk * sound.stream->frame_size);
            ma_pcm_rb_commit_write(&source.ring, chunk);

            src_ptr += chunk * sound.stream->frame_size;
            frames_remaining -= chunk;
        }

        if (frames_remaining > 0) {
            std::cerr << "[AudioPlayer] ring buffer write underrun for stream_id: " << stream_id
                      << " missing_frames=" << frames_remaining << " writable_capacity=" << ma_pcm_rb_available_write(&source.ring)
                      << std::endl;
        }

        source.ended = false;
    }
}

bool AudioPlayerService::configure_playback_device() {
    if (config_.device.empty()) {
        return false;
    }

    ma_context_config ctx_cfg = ma_context_config_init();
    if (ma_context_init(nullptr, 0, &ctx_cfg, &audio_context_) != MA_SUCCESS) {
        return false;
    }

    ma_device_info* playback_infos = nullptr;
    ma_uint32 playback_count = 0;
    if (ma_context_get_devices(&audio_context_, &playback_infos, &playback_count, nullptr, nullptr) != MA_SUCCESS) {
        ma_context_uninit(&audio_context_);
        return false;
    }

    for (ma_uint32 i = 0; i < playback_count; ++i) {
        if (device_matches(config_.device, &playback_infos[i])) {
            playback_device_id_ = playback_infos[i].id;
            has_custom_device_id_ = true;
            audio_context_ready_ = true;
            return true;
        }
    }

    ma_context_uninit(&audio_context_);
    return false;
}

void AudioPlayerService::cleanup() {
    if (cleanup_done_) {
        return;
    }
    cleanup_done_ = true;

    stop_all_sounds();
    if (engine_ready_) {
        ma_engine_uninit(&engine_);
        engine_ready_ = false;
    }

    if (sockets_ready_) {
        try {
            if (stream_socket_ready_) {
                stream_pull_.close();
                stream_socket_ready_ = false;
            }
            cmd_rep_.close();
            status_pub_.close();
        } catch (const zmq::error_t& e) {
            std::cerr << "[AudioPlayer] cleanup zmq error: " << e.what() << std::endl;
        }
        sockets_ready_ = false;
    }

    if (audio_context_ready_) {
        ma_context_uninit(&audio_context_);
        audio_context_ready_ = false;
    }
}

// 🆕 重复播放逻辑更新
void AudioPlayerService::update_repeat_sounds() {
    const uint64_t current_time = now_millis();
    
    for (auto& sound : active_sounds_) {
        const auto& ctrl = sound.playback_control;
        if (ctrl.playback_mode == "single") {
            // 单次播放，无需处理
            continue;
        }

    // 检查当前音频是否已播放完成
    const bool is_playing = ma_sound_is_playing(sound.handle.get());
    const bool at_end = ma_sound_at_end(sound.handle.get());
    const bool is_stopped = (!is_playing && at_end);

        if (ctrl.playback_mode == "repeat_count") {
            // 按播放次数重复
            // 使用帧位置检测播放完成（比 is_playing 更可靠）
            ma_uint64 cursor_frame = 0;
            ma_uint64 length_frames = 0;
            ma_sound_get_cursor_in_pcm_frames(sound.handle.get(), &cursor_frame);
            ma_sound_get_length_in_pcm_frames(sound.handle.get(), &length_frames);
            bool playback_finished = at_end || is_stopped;
            if (!playback_finished && length_frames > 0) {
                // 帧位置兜底判断，防止 at_end 失效
                playback_finished = (cursor_frame + 1 >= length_frames);
            }
            
            std::cerr << "[AudioPlayer] repeat_count check: alias=" << sound.alias
                      << " is_playing=" << is_playing
                      << " is_stopped=" << is_stopped
                      << " at_end=" << at_end
                      << " frame_complete=" << (cursor_frame + 1 >= length_frames)
                      << " cursor=" << cursor_frame << " length=" << length_frames
                      << " current_count=" << sound.playback_control.current_play_count
                      << " target_count=" << sound.playback_control.play_count
                      << " in_interval=" << sound.playback_control.is_in_repeat_interval << std::endl;
            
            if (playback_finished) {
                if (sound.playback_control.is_in_repeat_interval) {
                    // 检查间隔是否已过
                    uint64_t interval_elapsed = current_time - sound.playback_control.interval_start_ts;
                    std::cerr << "[AudioPlayer] interval check: elapsed=" << interval_elapsed 
                              << "ms / " << sound.playback_control.repeat_interval_ms << "ms" << std::endl;
                    
                    if (interval_elapsed >= sound.playback_control.repeat_interval_ms) {
                        sound.playback_control.is_in_repeat_interval = false;
                        
                        // 检查是否需要继续重复
                        if (sound.playback_control.current_play_count < sound.playback_control.play_count) {
                            // 重启播放
                            ma_sound_seek_to_pcm_frame(sound.handle.get(), 0);
                            ma_result r = ma_sound_start(sound.handle.get());
                            if (r == MA_SUCCESS) {
                                sound.playback_control.current_play_count++;
                                std::cerr << "[AudioPlayer] repeat: alias=" << sound.alias
                                          << " count=" << sound.playback_control.current_play_count 
                                          << "/" << sound.playback_control.play_count << std::endl;
                            } else {
                                std::cerr << "[AudioPlayer] failed to restart repeat: " << sound.alias << " code=" << r << std::endl;
                            }
                        }
                    }
                } else {
                    // 播放完成，进入间隔期
                    if (sound.playback_control.current_play_count < sound.playback_control.play_count) {
                        sound.playback_control.is_in_repeat_interval = true;
                        sound.playback_control.interval_start_ts = current_time;
                        std::cerr << "[AudioPlayer] entering repeat interval: alias=" << sound.alias
                                  << " after play " << sound.playback_control.current_play_count << std::endl;
                    }
                }
            }
        } else if (ctrl.playback_mode == "repeat_duration") {
            // 按时长循环
            const uint64_t elapsed = current_time - sound.playback_control.playback_start_ts;
            
            // 使用帧位置检测播放完成（比 is_playing 更可靠）
            ma_uint64 cursor_frame = 0;
            ma_uint64 length_frames = 0;
            ma_sound_get_cursor_in_pcm_frames(sound.handle.get(), &cursor_frame);
            ma_sound_get_length_in_pcm_frames(sound.handle.get(), &length_frames);
            bool playback_finished = at_end || is_stopped;
            if (!playback_finished && length_frames > 0) {
                playback_finished = (cursor_frame + 1 >= length_frames);
            }
            
            std::cerr << "[AudioPlayer] repeat_duration check: alias=" << sound.alias
                      << " is_playing=" << is_playing
                      << " is_stopped=" << is_stopped
                      << " at_end=" << at_end
                      << " frame_complete=" << (cursor_frame + 1 >= length_frames)
                      << " cursor=" << cursor_frame << " length=" << length_frames
                      << " elapsed=" << elapsed << "ms / " << sound.playback_control.play_duration_ms << "ms" << std::endl;
            
            if (elapsed >= sound.playback_control.play_duration_ms) {
                // 已达到时长限制，如果当前未在播放，标记为应完成
                if (playback_finished) {
                    std::cerr << "[AudioPlayer] repeat_duration complete: alias=" << sound.alias
                              << " elapsed=" << elapsed << "ms / " << sound.playback_control.play_duration_ms << "ms" << std::endl;
                    // 音频将在 prune_finished_sounds 中被清理
                }
            } else {
                // 未达到时长限制，检查是否需要重启
                if (playback_finished) {
                    // 重启播放
                    ma_sound_seek_to_pcm_frame(sound.handle.get(), 0);
                    ma_result r = ma_sound_start(sound.handle.get());
                    if (r == MA_SUCCESS) {
                        sound.playback_control.current_play_count++;
                        std::cerr << "[AudioPlayer] repeat_duration restart: alias=" << sound.alias
                                  << " count=" << sound.playback_control.current_play_count
                                  << " elapsed=" << elapsed << "ms / " << sound.playback_control.play_duration_ms << "ms" << std::endl;
                    } else {
                        std::cerr << "[AudioPlayer] failed to restart repeat_duration: " << sound.alias << " code=" << r << std::endl;
                    }
                }
            }
        }
    }
}

void AudioPlayerService::stop_by_id(const std::string& id) {
    if (id.empty()) {
        return;
    }

    bool removed = false;
    for (auto it = active_sounds_.begin(); it != active_sounds_.end();) {
        if (it->id == id) {
            if (it->handle) {
                ma_sound_stop(it->handle.get());
            }
            capture_control_dec();
            it = active_sounds_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }

    if (removed) {
        apply_ducking_state();
    }
}

void AudioPlayerService::stop_by_uri(const std::string& uri) {
    if (uri.empty()) {
        return;
    }

    bool removed = false;
    for (auto it = active_sounds_.begin(); it != active_sounds_.end();) {
        if (it->uri == uri) {
            if (it->handle) {
                ma_sound_stop(it->handle.get());
            }
            capture_control_dec();
            it = active_sounds_.erase(it);
            removed = true;
        } else {
            ++it;
        }
    }

    if (removed) {
        apply_ducking_state();
    }
}

std::string AudioPlayerService::generate_sound_id() {
    return std::to_string(next_sound_id_++);
}

} // namespace audio
} // namespace doly
