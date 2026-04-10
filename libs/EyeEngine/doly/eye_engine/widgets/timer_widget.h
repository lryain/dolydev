#pragma once

#include "widget_interface.h"
#include "doly/eye_engine/widgets/widget_audio_player.h"
#include "doly/eye_engine/widgets/widget_tts_helper.h"
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <cstdint>

#include <nlohmann/json.hpp>

namespace doly {
namespace eye_engine {

// 计时器模式
enum class TimerMode {
    Countdown,   // 倒计时
    Countup      // 正计时
};

// 计时器状态
enum class TimerState {
    Idle,        // 空闲（未启动）
    Running,     // 运行中
    Paused,      // 暂停
    Finished     // 完成（仅倒计时）
};

// 颜色结构（与 WeatherWidget 共享）
struct TimerColor {
    uint8_t r, g, b;
    uint8_t a = 255;
};

// Timer Widget 样式（7 段数字显示）
struct TimerStyle {
    TimerColor digit_color = {255, 180, 0, 255};        // 数字颜色（橙色）
    TimerColor warning_color = {255, 0, 0, 255};        // 警告颜色（红色，倒计时 <10s）
    TimerColor background_color = {0, 0, 0, 0};         // 背景色（透明）
    std::string layout = "split";                        // "split" 或 "single"
    bool colon_blink = true;                             // 冒号是否闪烁
    // 🆕 字体缩放百分比（基于自适应尺寸再乘以该比例），建议 50-150
    int digit_scale_percent = 100;
};

struct TimerAudioConfig {
    bool enabled = false;
    bool tick_sound_en = true;                      // 是否启用普通滴答音
    bool alarm_sound_en = true;                     // 是否启用闹钟音效
    bool timeup_sound_en = true;                    // 是否启用时间到提示音
    bool speak_last_sec_en = true;                // 是否在最后几秒播报
    int speak_last_sec_start = 10;                // 剩余多少秒开始播报
    std::string tick_topic = "event.eye.timer.tick";
    std::string finished_topic = "event.eye.timer.finished";
    std::string cmd_endpoint = "ipc:///tmp/doly_audio_player_cmd.sock"; // 播放服务 REQ 地址
    std::string tick_sound;                      // 普通滴答音
    std::string timeup_sound;                    // 时间到时播放一次的音
    std::string alarm_sound;                     // 时间到的闹钟音
    int tick_priority = 30;                      // Tick 播报优先级
    int timeup_priority = 40;                    // 时间到音优先级
    int final_priority = 60;                     // 结束/闹钟优先级
    int alarm_play_count = 1;                    // 闹钟重复播放次数（解决无法主动停止的问题）
    int alarm_repeat_interval_ms = 0;            // 闹钟重复播放间隔（ms），0 表示紧接着播放
    int req_timeout_ms = 150;                    // ZeroMQ 请求超时
    std::string tick_tts_template = "倒计时{seconds}秒";
    std::string final_tts = "倒计时结束";
    std::string tts_engine = "edge-tts";         // edge-tts | espeak-ng
    std::string espeak_command = "espeak-ng -v zh '{text}' >/dev/null 2>&1";
    // 🆕 edge-tts 配置
    bool edge_tts_enabled = false;               // 是否使用 edge-tts 播报（替代本地 TTS）
    std::string edge_tts_endpoint = "ipc:///tmp/doly_tts_req.sock"; // TTS 服务地址
    std::string edge_tts_client_bin = "python3 /home/pi/dev/nora-xiaozhi-dev/libs/tts/tts-server/edge_tts_client.py"; // 客户端可执行
    std::string edge_tts_voice = "zh-CN-YunxiaNeural";  // 默认发音人
    std::string edge_tts_pitch = "+0Hz";                // 默认音调
    std::string edge_tts_rate = "+0%";                 // 默认语速
    std::string edge_tts_volume = "+0%";               // 默认音量
    std::string edge_tts_format = "wav";                // 输出格式
    bool edge_tts_play = true;                           // 服务端直接播放
    int edge_tts_retries = 1;                            // 调用重试次数
    std::string edge_tts_transport = "zmq";            // zmq | cmd
    std::string edge_tts_play_mode = "audio_player";   // audio_player | local
    float edge_tts_ap_volume = 1.0f;                    // audio_player 音量
    std::string edge_tts_alias = "timer_tts";          // audio_player alias
    bool edge_tts_ducking = true;                       // 是否对其它音频进行 ducking
    int edge_tts_timeout_ms = 10000;                    // ZMQ 超时
};

// Timer Widget 实现
class TimerWidget : public WidgetBase {
public:
    using EventEmitter = std::function<void(const std::string&, const nlohmann::json&)>;

    TimerWidget();
    ~TimerWidget() override = default;

    // IWidget 接口实现
    void onShow(const nlohmann::json& config) override;
    void onHide() override;
    void update(double delta_time_ms) override;
    void render(FrameBuffer& buffer, const WidgetContext& ctx) override;
    ROI getUpdateROI() const override;
    bool needsRedraw() const override { return needs_redraw_; }
    bool updateConfig(const std::string& config_json) override;
    std::string getConfig() const override;
    void reset() override;

    // Timer 控制命令
    void start();                                  // 启动计时器
    void pause();                                  // 暂停
    void resume();                                 // 恢复
    void resetTimer();                             // 重置计时器（内部实现）
    void stop();                                   // 停止并隐藏（如果 auto_hide）
    
    // Timer 配置
    void setMode(TimerMode mode);
    void setDuration(int duration_sec);            // 设置倒计时时长（秒）
    void setAutoHide(bool auto_hide);              // 设置倒计时结束后自动隐藏
    void setTimeout(int timeout_sec);              // 设置在自动隐藏前的停留时间（秒）
    void setStyle(const TimerStyle& style);
    void setAudioConfig(const TimerAudioConfig& audio_config) { audio_config_ = audio_config; }
    void setEventEmitter(EventEmitter emitter) { event_emitter_ = std::move(emitter); }
    
    // Timer 状态查询
    TimerMode getMode() const { return mode_; }
    TimerState getState() const { return state_; }
    int getElapsedSeconds() const;                 // 获取已过时间（秒）
    int getRemainingSeconds() const;               // 获取剩余时间（秒，仅倒计时）
    bool isFinished() const { return state_ == TimerState::Finished; }
    bool autoHideEnabled() const { return auto_hide_; }
    bool autoShowEnabled() const { return auto_show_remaining_sec_ >= 0; }

private:
    // 7 段数字渲染方法
    void renderSplit(FrameBuffer& buffer, const WidgetContext& ctx, int minutes, int seconds,
                     uint16_t fg_color, uint16_t bg_color) const;
    void renderSingle(FrameBuffer& buffer, const WidgetContext& ctx, int minutes, int seconds,
                      uint16_t fg_color, uint16_t bg_color) const;
    void drawDigit(uint16_t* buffer, int width, int height, int digit, int x, int y,
                   float scale, uint16_t color) const;
    void drawColon(uint16_t* buffer, int width, int height, int x, int y, float scale,
                   uint16_t color) const;
    
    // 颜色选择（根据剩余时间）
    TimerColor getCurrentColor() const;
    void emitCountdownTick(int remaining_seconds);
    void emitCountdownFinished();
    void finalizeCountdown();
    void playClip(const std::string& clip, int priority, bool preempt,
                  const std::string& playback_mode = "single",
                  int play_count = 1,
                  uint32_t repeat_interval_ms = 0,
                  uint32_t repeat_fade_out_ms = 0,
                  uint64_t play_duration_ms = 0);
    void stopClip(const std::string& clip);
    static Anchor anchorFromString(const std::string& value);
    static std::string anchorToString(Anchor anchor);
    static TimerColor parseColor(const nlohmann::json& node, const TimerColor& fallback);
    static std::string formatTemplate(const std::string& templ, int seconds);
    void applyStyleConfig(const nlohmann::json& style_json);
    void applyPositionConfig(const nlohmann::json& position_json);
    void applyAudioConfig(const nlohmann::json& audio_json);
    WidgetTtsConfig buildTtsConfig(int priority) const;
    void handleCommand(const nlohmann::json& command_json);
    void emitAutoShowEvent(int remaining_seconds);

private:
    // 计时器配置
    TimerMode mode_ = TimerMode::Countdown;
    int duration_sec_ = 60;                        // 倒计时时长（秒）
    bool auto_hide_ = true;                        // 倒计时结束后自动隐藏
    TimerStyle style_;
    TimerAudioConfig audio_config_;
    EventEmitter event_emitter_;
    
    // 计时器状态
    TimerState state_ = TimerState::Idle;
    double elapsed_time_ms_ = 0.0;                 // 已过时间（毫秒）
    
    // 7 段数字显示状态
    std::array<bool, 2> side_dirty_ = {true, true};  // 左右眼脏标记
    int last_time_seconds_ = -1;                      // 上一次显示的秒数
    bool colon_visible_ = true;                       // 冒号可见性
    
    // 事件相关
    int last_announced_remaining_ = std::numeric_limits<int>::max();
    bool final_event_emitted_ = false;
    bool auto_start_ = false;
    int timer_timeout_ms_ = 0;                     // 自动隐藏时停留显示的时长（毫秒）
    int auto_show_remaining_sec_ = -1;            // 剩余多少秒时自动显示并恢复 tick
    double finished_elapsed_ms_ = 0.0;
    bool pending_dirty_after_show_ = false;

    // 音频播放相关：懒加载的 ZeroMQ REQ Socket
    WidgetAudioPlayer audio_player_{"timer_widget"};
};

inline constexpr const char* kTimerAutoShowTopic = "event.eye.timer.auto_show";

} // namespace eye_engine
} // namespace doly
